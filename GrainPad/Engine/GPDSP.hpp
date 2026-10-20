// Small real-time DSP building blocks. No allocation after prepare().
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace gp {

constexpr float kPi = 3.14159265358979f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

// Fast deterministic RNG (xorshift32), real-time safe.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (next() >> 8) * (1.0f / 16777216.0f); }        // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }                          // -1..1
};

// Cubic Hermite interpolation; reads are clamped to the buffer.
inline float readCubic(const float* d, int n, double pos) {
    int i = (int)std::floor(pos);
    float t = (float)(pos - i);
    auto at = [&](int k) { return d[k < 0 ? 0 : (k >= n ? n - 1 : k)]; };
    float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

// Topology-preserving state variable filter (Zavalishin).
struct SVF {
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;
    void set(float cutoffHz, float q, float sr) {
        cutoffHz = clampf(cutoffHz, 10.0f, sr * 0.49f);
        g = std::tan(kPi * cutoffHz / sr);
        k = 1.0f / std::max(q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns low, writes band and high
    inline void tick(float x, float& lp, float& bp, float& hp) {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    inline float lp(float x) { float l, b, h; tick(x, l, b, h); return l; }
    inline float hp(float x) { float l, b, h; tick(x, l, b, h); return h; }
};

struct OnePole {
    float a = 0, z = 0;
    void setHz(float hz, float sr) { a = std::exp(-2.0f * kPi * hz / sr); }
    inline float lp(float x) { z = x + a * (z - x); return z; }
};

struct Smoother {
    float cur = 0, target = 0, coef = 0.999f;
    void setTime(float seconds, float sr) { coef = std::exp(-1.0f / std::max(1.0f, seconds * sr)); }
    void snap(float v) { cur = target = v; }
    inline float next() { cur = target + coef * (cur - target); return cur; }
};

// Fractional delay line with linear interpolation.
struct DelayLine {
    std::vector<float> buf; int w = 0, n = 1;
    void prepare(int size) { n = std::max(size, 4); buf.assign(n, 0.0f); w = 0; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    inline void push(float x) { buf[w] = x; if (++w >= n) w = 0; }
    inline float read(float delay) const {
        delay = clampf(delay, 1.0f, (float)(n - 2));
        float rp = (float)w - delay;
        while (rp < 0) rp += (float)n;
        int i = (int)rp; float f = rp - i;
        if (i >= n) i -= n;
        int j = i + 1; if (j >= n) j = 0;
        return buf[i] + f * (buf[j] - buf[i]);
    }
};

struct Allpass {
    std::vector<float> buf; int idx = 0; float g = 0.6f;
    void prepare(int len, float gain) { buf.assign(std::max(len, 1), 0.0f); idx = 0; g = gain; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    inline float tick(float x) {
        float d = buf[idx];
        float y = -g * x + d;
        buf[idx] = x + g * y;
        if (++idx >= (int)buf.size()) idx = 0;
        return y;
    }
};

// 8-line feedback delay network reverb with input diffusion.
struct Reverb {
    static constexpr int N = 8;
    DelayLine lines[N];
    OnePole damp[N];
    Allpass diff[4];
    float lens[N] = {};
    float sr = 48000, fb = 0.8f;
    float lfo = 0;
    void prepare(float sampleRate) {
        sr = sampleRate;
        static const float base[N] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};
        float scale = sr / 44100.0f;
        for (int i = 0; i < N; ++i) lines[i].prepare((int)(base[i] * scale * 2.6f) + 64);
        static const int ap[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; ++i) diff[i].prepare((int)(ap[i] * scale), 0.65f);
        setParams(0.6f, 0.4f);
    }
    void clear() { for (auto& l : lines) l.clear(); for (auto& d : diff) d.clear(); for (auto& d : damp) d.z = 0; }
    void setParams(float size, float damping) {
        static const float base[N] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};
        float scale = sr / 44100.0f * (0.45f + 2.0f * size);
        for (int i = 0; i < N; ++i) lens[i] = base[i] * scale;
        fb = 0.70f + 0.27f * size;
        float hz = 16000.0f * std::pow(0.06f, damping);
        for (auto& d : damp) d.setHz(hz, sr);
    }
    inline void tick(float inL, float inR, float& outL, float& outR) {
        float x = (inL + inR) * 0.5f;
        for (auto& a : diff) x = a.tick(x);
        lfo += 0.6f / sr; if (lfo > 1) lfo -= 1;
        float mod = std::sin(2.0f * kPi * lfo) * 6.0f;
        float o[N];
        for (int i = 0; i < N; ++i) o[i] = damp[i].lp(lines[i].read(lens[i] + (i & 1 ? mod : -mod)));
        // Hadamard 8x8 (fast Walsh) mixing
        float h[N]; for (int i = 0; i < N; ++i) h[i] = o[i];
        for (int len = 1; len < N; len <<= 1)
            for (int i = 0; i < N; i += len << 1)
                for (int j = i; j < i + len; ++j) { float a = h[j], b = h[j + len]; h[j] = a + b; h[j + len] = a - b; }
        const float norm = 0.35355339f; // 1/sqrt(8)
        for (int i = 0; i < N; ++i) lines[i].push(x * (i < 4 ? 0.5f : -0.5f) + h[i] * norm * fb);
        outL = (o[0] + o[2] + o[4] + o[6]) * 0.5f;
        outR = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
    }
};

} // namespace gp
