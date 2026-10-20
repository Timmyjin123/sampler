// Synthesized kits: every sound is generated in code, so the app ships with
// no audio files and no licensing questions. 7 kits x 16 sounds + demo patterns.
#include "GPEngine.h"
#include "GPDSP.hpp"
#include <cstring>
#include <functional>
#include <vector>

using namespace gp;

namespace {
const double SR = 48000.0;
using Buf = std::vector<float>;

Buf make(double sec) { return Buf((size_t)(sec * SR), 0.0f); }
inline double T(size_t i) { return i / SR; }
double midiHz(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
float saw(double ph) { ph -= std::floor(ph); return (float)(2 * ph - 1); }
float sqr(double ph, double pw = 0.5) { ph -= std::floor(ph); return ph < pw ? 1.0f : -1.0f; }
float tri(double ph) { ph -= std::floor(ph); return (float)(ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph); }

struct Noise {
    Rng r;
    explicit Noise(uint32_t seed = 12345) : r(seed) {}
    float operator()() { return r.bi(); }
};

void normalize(Buf& b, float peak = 0.89f) {
    float m = 0; for (float v : b) m = std::max(m, std::fabs(v));
    if (m > 1e-6f) { float g = peak / m; for (float& v : b) v *= g; }
}
void fadeOut(Buf& b, double sec = 0.01) {
    size_t n = std::min(b.size(), (size_t)(sec * SR));
    for (size_t i = 0; i < n; ++i) b[b.size() - 1 - i] *= (float)i / n;
}
// lo-fi: bit reduction, sample-rate reduction, low-pass, a little noise
void lofi(Buf& b, int bits, int hold, float lpHz, float noise = 0.004f) {
    SVF lp; lp.set(lpHz, 0.7f, (float)SR); Noise n(99);
    float levels = (float)(1 << bits), held = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i % hold == 0) held = std::round(b[i] * levels) / levels;
        b[i] = lp.lp(held) + n() * noise;
    }
}
void saturate(Buf& b, float drive) { for (float& v : b) v = std::tanh(v * drive) / std::tanh(drive); }

// ---------------------------------------------------------------- drums
Buf kick(double f0, double f1, double pitchDecay, double ampDecay, double click, double drive, double len = 0.7) {
    Buf b = make(len); double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i), f = f1 + (f0 - f1) * std::exp(-t * pitchDecay);
        ph += 2 * M_PI * f / SR;
        double body = std::sin(ph) * std::exp(-t * ampDecay);
        double cl = std::exp(-t * 500) * click;
        b[i] = (float)std::tanh((body + cl) * drive) / (float)std::tanh(drive);
    }
    fadeOut(b); return b;
}
Buf snare(double toneHz, double toneDecay, double noiseDecay, double bpHz, double noiseAmt, double len = 0.4, uint32_t seed = 1) {
    Buf b = make(len); Noise n(seed); SVF bp; bp.set((float)bpHz, 0.7f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        double tone = (std::sin(2 * M_PI * toneHz * t) + 0.5 * std::sin(2 * M_PI * toneHz * 1.78 * t)) * std::exp(-t * toneDecay);
        float l, bb, h; bp.tick(n(), l, bb, h);
        double nz = (bb * 1.4 + h * 0.6) * std::exp(-t * noiseDecay) * noiseAmt;
        b[i] = (float)std::tanh((tone * 0.6 + nz) * 1.5);
    }
    normalize(b, 0.85f); fadeOut(b); return b;
}
Buf hat(double decay, double hpHz, double len, double metal = 0.5, uint32_t seed = 7) {
    Buf b = make(len); Noise n(seed); SVF hp; hp.set((float)hpHz, 0.8f, (float)SR);
    static const double fr[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i); float m = 0;
        for (double f : fr) m += sqr(f * 8.0 * t);
        float x = (float)(m / 6 * metal + n() * (1 - metal * 0.5));
        double env = std::exp(-t / decay) * std::min(1.0, t * 3000);
        b[i] = hp.hp(x) * (float)env;
    }
    normalize(b, 0.6f); fadeOut(b, 0.005); return b;
}
Buf clap(double bpHz, double tailDecay, int bursts = 3, double spread = 0.011, uint32_t seed = 3) {
    Buf b = make(0.5); Noise n(seed); SVF bp; bp.set((float)bpHz, 1.3f, (float)SR);
    double tailStart = bursts * spread;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i), e = 0;
        for (int k = 0; k < bursts; ++k) { double tk = t - k * spread; if (tk >= 0) e += std::exp(-tk * 190); }
        double tail = t > tailStart ? std::exp(-(t - tailStart) * tailDecay) : 0;
        float l, bb, h; bp.tick(n(), l, bb, h);
        b[i] = bb * (float)(e * 0.8 + tail * 0.7);
    }
    normalize(b, 0.85f); fadeOut(b); return b;
}
Buf rim(double f = 1700) {
    Buf b = make(0.12);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        b[i] = (float)((std::sin(2 * M_PI * f * t) * 0.6 + std::sin(2 * M_PI * f * 0.48 * t) * 0.4) * std::exp(-t * 55));
    }
    normalize(b, 0.8f); return b;
}
Buf tom(double f, double decay = 7, double bend = 60) {
    Buf b = make(0.6); double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i), ff = f + bend * std::exp(-t * 20);
        ph += 2 * M_PI * ff / SR;
        b[i] = (float)(std::sin(ph) * std::exp(-t * decay));
    }
    normalize(b, 0.85f); fadeOut(b); return b;
}
Buf cowbell() {
    Buf b = make(0.5); SVF bp; bp.set(2600, 1.2f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        float x = sqr(540 * t) + sqr(800 * t);
        double env = t < 0.01 ? 1.0 : 0.35 + 0.65 * std::exp(-(t - 0.01) * 30);
        env *= std::exp(-t * 7);
        float l, bb, h; bp.tick(x, l, bb, h);
        b[i] = (float)((bb * 0.8 + l * 0.2) * env);
    }
    normalize(b, 0.7f); fadeOut(b); return b;
}
Buf conga(double f, double decay = 14) {
    Buf b = make(0.4); double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i), ff = f * (1 + 0.25 * std::exp(-t * 60));
        ph += 2 * M_PI * ff / SR;
        b[i] = (float)(std::sin(ph) * std::exp(-t * decay) + std::sin(ph * 2.01) * 0.15 * std::exp(-t * 40));
    }
    normalize(b, 0.8f); fadeOut(b); return b;
}
Buf shaker(double len = 0.18, double attack = 0.03, uint32_t seed = 5) {
    Buf b = make(len); Noise n(seed); SVF hp; hp.set(5500, 0.7f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        double env = t < attack ? t / attack : std::exp(-(t - attack) * 28);
        b[i] = hp.hp(n()) * (float)env;
    }
    normalize(b, 0.5f); fadeOut(b, 0.005); return b;
}
Buf cymbal(double len, double decay, double hpHz, uint32_t seed = 11) {
    Buf b = make(len); Noise n(seed); SVF hp; hp.set((float)hpHz, 0.6f, (float)SR);
    static const double fr[6] = {320, 465, 572, 717, 845, 1013};
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i); float m = 0;
        for (double f : fr) m += sqr(f * 4.7 * t);
        float x = m / 6 * 0.4f + n() * 0.8f;
        b[i] = hp.hp(x) * (float)(std::exp(-t / decay) * std::min(1.0, t * 800));
    }
    normalize(b, 0.55f); fadeOut(b, 0.05); return b;
}
Buf snap() {
    Buf b = make(0.15); Noise n(21); SVF bp; bp.set(2200, 2.0f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        float l, bb, h; bp.tick(n(), l, bb, h);
        b[i] = bb * (float)std::exp(-t * 60) + (float)(std::sin(2 * M_PI * 1300 * t) * std::exp(-t * 120) * 0.3);
    }
    normalize(b, 0.75f); return b;
}
Buf fmPerc(double carrier, double ratio, double index, double decay) {
    Buf b = make(0.35);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        double mod = std::sin(2 * M_PI * carrier * ratio * t) * index * std::exp(-t * decay * 1.5);
        b[i] = (float)(std::sin(2 * M_PI * carrier * t + mod) * std::exp(-t * decay));
    }
    normalize(b, 0.75f); fadeOut(b); return b;
}

// ---------------------------------------------------------------- tonal
Buf bass808(double note, double decay, double drive, double glide = 0) {
    Buf b = make(1.8); double ph = 0, f = midiHz(note);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        double ff = f * std::pow(2.0, glide / 12.0 * std::exp(-t * 18)) * (1 + 0.6 * std::exp(-t * 45));
        ph += 2 * M_PI * ff / SR;
        double x = std::sin(ph) * std::exp(-t * decay) * std::min(1.0, t * 600);
        b[i] = (float)(std::tanh(x * drive) / std::tanh(drive));
    }
    fadeOut(b, 0.05); return b;
}
Buf synthBass(double note, int wave, double cutoff, double envAmt, double reso, double decay, double len = 0.7) {
    Buf b = make(len); SVF lp; double f = midiHz(note);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        if (i % 16 == 0) lp.set((float)(cutoff + envAmt * std::exp(-t * 14)), (float)reso, (float)SR);
        float x = wave == 0 ? saw(f * t) : (wave == 1 ? sqr(f * t) : tri(f * t));
        x += 0.4f * (wave == 0 ? saw(f * 0.5 * t) : sqr(f * 0.5 * t));
        b[i] = std::tanh(lp.lp(x) * 1.5f) * (float)(std::exp(-t * decay) * std::min(1.0, t * 400));
    }
    normalize(b, 0.85f); fadeOut(b, 0.02); return b;
}
Buf chord(std::initializer_list<double> notes, int wave, double cutoff, double attack, double decay, double len, double detune = 0.004) {
    Buf b = make(len); SVF lp; lp.set((float)cutoff, 0.9f, (float)SR);
    std::vector<double> ns(notes);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i); float x = 0;
        for (size_t k = 0; k < ns.size(); ++k) {
            double f = midiHz(ns[k]);
            if (wave == 0) x += saw(f * t + k * 0.17) + 0.7f * saw(f * (1 + detune) * t);
            else if (wave == 1) x += sqr(f * t + k * 0.11, 0.35) + 0.6f * sqr(f * (1 - detune) * t);
            else x += (float)std::sin(2 * M_PI * f * t) + 0.3f * (float)std::sin(4 * M_PI * f * t);
        }
        double env = std::min(1.0, t / std::max(0.001, attack)) * std::exp(-t * decay);
        b[i] = lp.lp(x / ns.size()) * (float)env;
    }
    normalize(b, 0.8f); fadeOut(b, 0.05); return b;
}
Buf rhodes(std::initializer_list<double> notes, double len = 2.2) {
    Buf b = make(len); std::vector<double> ns(notes);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i), x = 0;
        for (double n : ns) {
            double f = midiHz(n);
            double mod = std::sin(2 * M_PI * f * t) * 1.3 * std::exp(-t * 3.5);
            x += std::sin(2 * M_PI * f * t + mod) * (1 + 0.15 * std::sin(2 * M_PI * 4.5 * t));
        }
        b[i] = (float)(x / ns.size() * std::exp(-t * 1.3) * std::min(1.0, t * 300));
    }
    normalize(b, 0.8f); fadeOut(b, 0.08); return b;
}
Buf pluck(double note, double bright = 3500, double decay = 3.5) {
    Buf b = make(0.9); SVF lp; double f = midiHz(note);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        if (i % 32 == 0) lp.set((float)(200 + bright * std::exp(-t * 12)), 2.0f, (float)SR);
        float x = saw(f * t) + 0.5f * saw(f * 1.005 * t);
        b[i] = lp.lp(x) * (float)std::exp(-t * decay);
    }
    normalize(b, 0.8f); fadeOut(b, 0.03); return b;
}
Buf bell(double note, double ratio = 3.5, double decay = 2.2) {
    Buf b = make(2.4); double f = midiHz(note);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        double mod = std::sin(2 * M_PI * f * ratio * t) * 2.0 * std::exp(-t * 2.5);
        b[i] = (float)(std::sin(2 * M_PI * f * t + mod) * std::exp(-t * decay));
    }
    normalize(b, 0.7f); fadeOut(b, 0.1); return b;
}
Buf vox(double note, double f1, double f2, double f3, double len = 1.4) {
    Buf b = make(len); SVF a, c, d;
    a.set((float)f1, 6, (float)SR); c.set((float)f2, 6, (float)SR); d.set((float)f3, 7, (float)SR);
    double f0 = midiHz(note);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        double vib = 1.0 + 0.006 * std::sin(2 * M_PI * 5.2 * t) * std::min(1.0, t * 2);
        float src = saw(f0 * vib * t);
        float l, b1, b2, b3, h;
        a.tick(src, l, b1, h); c.tick(src, l, b2, h); d.tick(src, l, b3, h);
        double env = std::min(1.0, t * 12) * std::min(1.0, (len - t) / 0.3);
        b[i] = (b1 + b2 * 0.6f + b3 * 0.3f) * (float)std::max(0.0, env);
    }
    normalize(b, 0.8f); return b;
}
Buf drone(std::initializer_list<double> notes, double len, double cutoff, double move, uint32_t seed = 4) {
    Buf b = make(len); Noise n(seed); SVF lp; std::vector<double> ns(notes);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        if (i % 64 == 0) lp.set((float)(cutoff * (1 + move * std::sin(2 * M_PI * 0.2 * t))), 1.2f, (float)SR);
        float x = 0;
        for (size_t k = 0; k < ns.size(); ++k) { double f = midiHz(ns[k]); x += saw(f * t + k * 0.13) + saw(f * 1.003 * t); }
        x = x / (2 * ns.size()) + n() * 0.03f;
        double env = std::min(1.0, t * 1.5) * std::min(1.0, (len - t) * 2);
        b[i] = lp.lp(x) * (float)env;
    }
    normalize(b, 0.8f); return b;
}
Buf glass(std::initializer_list<double> notes, double len) {
    Buf b = make(len); std::vector<double> ns(notes);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i), x = 0;
        for (size_t k = 0; k < ns.size(); ++k) {
            double f = midiHz(ns[k]);
            x += std::sin(2 * M_PI * f * t + std::sin(2 * M_PI * f * 2 * t) * 0.4) * (0.6 + 0.4 * std::sin(2 * M_PI * (0.1 + k * 0.07) * t));
        }
        double env = std::min(1.0, t / 0.8) * std::min(1.0, (len - t) / 0.8);
        b[i] = (float)(x / ns.size() * env);
    }
    normalize(b, 0.75f); return b;
}

// ---------------------------------------------------------------- fx
Buf riser(double len = 2.0) {
    Buf b = make(len); Noise n(31); SVF bp;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        if (i % 32 == 0) bp.set((float)(300 * std::pow(30.0, t / len)), 2.5f, (float)SR);
        float l, bb, h; bp.tick(n(), l, bb, h);
        b[i] = bb * (float)(std::pow(t / len, 1.5) * std::min(1.0, (len - t) / 0.05));
    }
    normalize(b, 0.8f); return b;
}
Buf impact() {
    Buf b = make(2.5); Noise n(41); SVF lp; double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        if (i % 32 == 0) lp.set((float)(8000 * std::exp(-t * 3) + 100), 0.7f, (float)SR);
        ph += 2 * M_PI * (40 + 120 * std::exp(-t * 6)) / SR;
        b[i] = (float)(std::sin(ph) * std::exp(-t * 1.8) * 0.8) + lp.lp(n()) * (float)std::exp(-t * 2.2) * 0.6f;
    }
    normalize(b, 0.85f); fadeOut(b, 0.1); return b;
}
Buf zap(double f0 = 3000, double f1 = 80, double speed = 25) {
    Buf b = make(0.35); double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i);
        ph += 2 * M_PI * (f1 + (f0 - f1) * std::exp(-t * speed)) / SR;
        b[i] = (float)(sqr(ph / (2 * M_PI)) * std::exp(-t * 10) * 0.6);
    }
    normalize(b, 0.7f); fadeOut(b); return b;
}
Buf vinyl(double len = 4.0) {
    Buf b = make(len); Noise n(77); Rng r(5); SVF bp; bp.set(1500, 0.5f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        float hiss = bp.lp(n()) * 0.05f;
        float crackle = (r.uni() < 0.0009f) ? r.bi() * 0.8f : 0.0f;
        b[i] = hiss + crackle;
    }
    SVF lp; lp.set(6000, 0.7f, (float)SR);
    for (float& v : b) v = lp.lp(v);
    normalize(b, 0.4f); return b;
}
Buf noiseSweep(double len, bool up) {
    Buf b = make(len); Noise n(51); SVF f;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = T(i), k = up ? t / len : 1 - t / len;
        if (i % 32 == 0) f.set((float)(200 * std::pow(60.0, k)), 3.0f, (float)SR);
        float l, bb, h; f.tick(n(), l, bb, h);
        b[i] = bb * (float)(std::sin(M_PI * t / len));
    }
    normalize(b, 0.7f); return b;
}

// ---------------------------------------------------------------- kit model
struct Sound {
    const char* name;
    std::function<Buf()> make;
    std::function<void(int)> tweak;   // pad params after loading
};
struct Pattern {
    int length;
    std::vector<std::pair<int, const char*>> rows;   // pad, steps ('x' = 1, 'o' = .65, 'g' = .35)
};
struct Kit {
    const char* name;
    const char* desc;
    float bpm, swing;
    std::vector<Sound> sounds;
    std::vector<Pattern> patterns;
    std::function<void()> fx;
};

auto P = [](int pad, GPPadParam p, float v) { gp_set_pad_param(pad, p, v); };
void none(int) {}
void hold(int p) { P(p, GP_P_ONESHOT, 0); P(p, GP_P_RELEASE, 0.25f); }
void mono808(int p) { P(p, GP_P_MONO, 1); P(p, GP_P_CHOKE, 2); P(p, GP_P_ONESHOT, 0); P(p, GP_P_RELEASE, 0.12f); }
void hatChoke(int p) { P(p, GP_P_CHOKE, 1); }
void verb(int p, float v) { P(p, GP_P_REVERB_SEND, v); }
void texture(int p, float size, float dens, float spray, float scan, float scatter) {
    P(p, GP_P_MODE, 1); P(p, GP_P_ONESHOT, 0); P(p, GP_P_ATTACK, 0.35f); P(p, GP_P_RELEASE, 1.6f);
    P(p, GP_P_GRAIN_SIZE, size); P(p, GP_P_GRAIN_DENSITY, dens); P(p, GP_P_GRAIN_SPRAY, spray);
    P(p, GP_P_GRAIN_SCAN, scan); P(p, GP_P_GRAIN_PITCHRND, scatter); P(p, GP_P_REVERB_SEND, 0.5f);
}
void setFx(float filter, float drive, float crush, float delayTime, float fb, float tone, float size, float damp) {
    gp_set_master(GP_M_FILTER, filter); gp_set_master(GP_M_DRIVE, drive); gp_set_master(GP_M_CRUSH, crush);
    gp_set_master(GP_M_DELAY_TIME, delayTime); gp_set_master(GP_M_DELAY_FB, fb); gp_set_master(GP_M_DELAY_TONE, tone);
    gp_set_master(GP_M_REVERB_SIZE, size); gp_set_master(GP_M_REVERB_DAMP, damp);
}

const std::vector<Kit>& kits() {
    static std::vector<Kit> k;
    if (!k.empty()) return k;

    // ---------------------------------------------------------- Boom Bap
    k.push_back({"Boom Bap", "Dusty 90s drums, sub, keys and a texture drone", 92, 0.12f, {
        {"Kick", [] { return kick(170, 46, 28, 6.5, 0.5, 1.6); }, none},
        {"Snare", [] { return snare(185, 28, 15, 3200, 1.0, 0.35); }, none},
        {"Hat", [] { return hat(0.035, 7500, 0.12); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.7f); }},
        {"Open Hat", [] { return hat(0.22, 7500, 0.6); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.65f); }},
        {"Clap", [] { return clap(1400, 18); }, [](int p) { verb(p, 0.25f); }},
        {"Rim", [] { return rim(); }, [](int p) { P(p, GP_P_PAN, -0.3f); }},
        {"Tom", [] { return tom(110); }, none},
        {"Shaker", [] { return shaker(); }, [](int p) { P(p, GP_P_PAN, 0.35f); P(p, GP_P_GAIN, 0.65f); }},
        {"Sub", [] { return bass808(36, 1.2, 1.8); }, mono808},
        {"Pluck", [] { return pluck(48); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.3f); }},
        {"Stab", [] { return chord({60, 63, 67, 74}, 0, 2400, 0.005, 4.0, 0.9); }, [](int p) { verb(p, 0.35f); P(p, GP_P_DELAY_SEND, 0.2f); }},
        {"Keys", [] { return rhodes({60, 63, 67, 70}); }, [](int p) { verb(p, 0.3f); }},
        {"Vox", [] { return vox(57, 800, 1150, 2900); }, [](int p) { hold(p); verb(p, 0.4f); }},
        {"Riser", [] { return riser(); }, none},
        {"Bell", [] { return bell(79); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.4f); verb(p, 0.4f); }},
        {"Drone", [] { return drone({48, 55, 62, 63, 70}, 4.0, 900, 0.6); }, [](int p) { texture(p, 0.2f, 22, 0.35f, 0.08f, 0.15f); }},
    }, {
        {16, {{0, "x......x..x....."}, {1, "....x.......x..."}, {2, "x.x.xgx.x.x.xgx."}, {3, "..............o."},
              {4, "............o..."}, {8, "x.........x....."}, {10, "x..............."}}},
        {16, {{0, "x......x..x...x."}, {1, "....x.......x.gx"}, {2, "x.x.x.x.x.x.x.x."}, {5, "...o......o....."},
              {7, "gxgxgxgxgxgxgxgx"}, {8, "x.........x...x."}, {11, "x.......o......."}}},
    }, [] { setFx(0.5f, 0, 0, 3, 0.35f, 0.5f, 0.5f, 0.4f); }});

    // ---------------------------------------------------------- Trap
    k.push_back({"Trap", "Long 808s in four keys, crisp snare, rolling hats", 140, 0.0f, {
        {"Kick", [] { return kick(220, 52, 40, 9, 0.8, 2.2, 0.5); }, none},
        {"Snare", [] { return snare(210, 35, 18, 4200, 1.2, 0.35, 9); }, [](int p) { verb(p, 0.2f); }},
        {"Clap", [] { return clap(1700, 22, 4, 0.008); }, [](int p) { verb(p, 0.25f); }},
        {"Hat", [] { return hat(0.025, 9000, 0.08, 0.6); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.65f); }},
        {"Open Hat", [] { return hat(0.3, 8500, 0.7, 0.6); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.6f); }},
        {"Rim", [] { return rim(1900); }, none},
        {"Snap", [] { return snap(); }, [](int p) { verb(p, 0.3f); }},
        {"Perc", [] { return fmPerc(900, 1.41, 4, 18); }, [](int p) { P(p, GP_P_PAN, 0.3f); }},
        {"808 C", [] { return bass808(36, 0.7, 2.5, 3); }, mono808},
        {"808 D#", [] { return bass808(39, 0.7, 2.5, 3); }, mono808},
        {"808 F", [] { return bass808(41, 0.7, 2.5, 3); }, mono808},
        {"808 G", [] { return bass808(43, 0.7, 2.5, 3); }, mono808},
        {"Bell", [] { return bell(84, 3.01, 2.8); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.35f); verb(p, 0.35f); }},
        {"Choir", [] { return vox(63, 700, 1220, 2600, 2.0); }, [](int p) { hold(p); verb(p, 0.6f); }},
        {"Riser", [] { return riser(1.8); }, none},
        {"Impact", [] { return impact(); }, [](int p) { verb(p, 0.3f); }},
    }, {
        {32, {{0, "x.......x.x.............x......."}, {1, "........x...............x......."},
              {3, "x.x.x.x.x.x.xxx.x.x.x.x.x.xxxxx."}, {4, "..............................o."},
              {8, "x...............x..............."}, {9, "..........x....................."},
              {10, "...........................x...."}, {12, "x..............................."}}},
        {32, {{0, "x......x..x.....x.......x.x....."}, {2, "........x...............x......."},
              {3, "x.xxx.x.x.x.x.xxx.x.xxx.x.x.xxxx"}, {6, "............x..............x...."},
              {8, "x......x........x..............."}, {11, "..........x.............x......."},
              {7, "...o.......o.......o.......o...."}}},
    }, [] { setFx(0.5f, 0.05f, 0, 5, 0.3f, 0.6f, 0.45f, 0.5f); }});

    // ---------------------------------------------------------- House
    k.push_back({"House", "909-style drums, organ bass, minor-7 stabs", 124, 0.08f, {
        {"Kick", [] { return kick(200, 50, 30, 8, 0.6, 2.0, 0.45); }, none},
        {"Clap", [] { return clap(1300, 14, 3, 0.012); }, [](int p) { verb(p, 0.3f); }},
        {"Hat", [] { return hat(0.04, 7000, 0.12, 0.7); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.6f); }},
        {"Open Hat", [] { return hat(0.18, 7000, 0.5, 0.7); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.6f); }},
        {"Ride", [] { return cymbal(1.6, 0.7, 5000); }, [](int p) { P(p, GP_P_GAIN, 0.55f); P(p, GP_P_PAN, 0.25f); }},
        {"Shaker", [] { return shaker(0.15, 0.02, 8); }, [](int p) { P(p, GP_P_PAN, -0.3f); P(p, GP_P_GAIN, 0.6f); }},
        {"Conga Hi", [] { return conga(330); }, [](int p) { P(p, GP_P_PAN, 0.35f); }},
        {"Conga Lo", [] { return conga(220); }, [](int p) { P(p, GP_P_PAN, -0.35f); }},
        {"Rim", [] { return rim(1600); }, none},
        {"Bass C", [] { return synthBass(36, 1, 300, 900, 1.5, 4.0, 0.5); }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Bass G", [] { return synthBass(43, 1, 300, 900, 1.5, 4.0, 0.5); }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Stab Cm7", [] { return chord({60, 63, 67, 70}, 1, 2200, 0.003, 5.0, 0.7); }, [](int p) { verb(p, 0.35f); P(p, GP_P_DELAY_SEND, 0.25f); }},
        {"Stab Fm7", [] { return chord({60, 65, 68, 75}, 1, 2200, 0.003, 5.0, 0.7); }, [](int p) { verb(p, 0.35f); P(p, GP_P_DELAY_SEND, 0.25f); }},
        {"Hey", [] { return vox(62, 600, 1700, 2600, 0.35); }, [](int p) { verb(p, 0.3f); }},
        {"Sweep", [] { return noiseSweep(2.0, true); }, none},
        {"Crash", [] { return cymbal(2.4, 0.9, 3500, 17); }, [](int p) { P(p, GP_P_GAIN, 0.6f); }},
    }, {
        {16, {{0, "x...x...x...x..."}, {1, "....x.......x..."}, {2, "x.x.x.x.x.x.x.x."}, {3, "..x...x...x...x."},
              {5, "gxgxgxgxgxgxgxgx"}, {9, "..x..x....x..x.."}, {11, "...x......x....."}}},
        {16, {{0, "x...x...x...x..."}, {1, "....x.......x..."}, {3, "..x...x...x...x."}, {4, "x.x.x.x.x.x.x.x."},
              {6, "...x..x.....x..."}, {7, "x.......x..x...."}, {9, "..x..x....x..x.."}, {12, "...x......x....."}}},
    }, [] { setFx(0.5f, 0.1f, 0, 4, 0.3f, 0.55f, 0.4f, 0.5f); }});

    // ---------------------------------------------------------- Techno
    k.push_back({"Techno", "Rumble kick, metallic percussion, acid bass", 130, 0.0f, {
        {"Kick", [] { return kick(180, 45, 25, 6, 0.4, 3.0, 0.7); }, none},
        {"Rumble", [] {
            Buf b = kick(120, 40, 15, 2.5, 0, 2.0, 1.2);
            SVF lp; lp.set(180, 1.5f, (float)SR); for (float& v : b) v = lp.lp(v);
            normalize(b, 0.8f); return b; }, [](int p) { P(p, GP_P_REVERB_SEND, 0.4f); P(p, GP_P_GAIN, 0.7f); }},
        {"Clap", [] { return clap(1200, 10, 3, 0.014, 13); }, [](int p) { verb(p, 0.4f); }},
        {"Hat", [] { return hat(0.03, 8000, 0.1, 0.9, 19); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.6f); }},
        {"Open Hat", [] { return hat(0.15, 8000, 0.45, 0.9, 19); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.6f); }},
        {"Ride", [] { return cymbal(1.2, 0.5, 6000, 23); }, [](int p) { P(p, GP_P_GAIN, 0.5f); }},
        {"Rim", [] { return rim(2100); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.3f); }},
        {"Perc 1", [] { return fmPerc(420, 2.7, 6, 22); }, [](int p) { P(p, GP_P_PAN, -0.4f); }},
        {"Perc 2", [] { return fmPerc(760, 1.33, 3, 30); }, [](int p) { P(p, GP_P_PAN, 0.4f); P(p, GP_P_DELAY_SEND, 0.25f); }},
        {"Acid C", [] { return synthBass(36, 0, 250, 2400, 6.0, 3.0, 0.4); }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Acid D#", [] { return synthBass(39, 0, 250, 2400, 6.0, 3.0, 0.4); }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Acid G", [] { return synthBass(43, 0, 250, 2400, 6.0, 3.0, 0.4); }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Stab", [] { return chord({48, 55, 58, 63}, 0, 1500, 0.002, 6.0, 0.6, 0.008); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.4f); verb(p, 0.3f); }},
        {"Sweep", [] { return noiseSweep(3.0, false); }, [](int p) { verb(p, 0.4f); }},
        {"Impact", [] { return impact(); }, none},
        {"Drone", [] { return drone({36, 43, 48, 51}, 4.0, 500, 0.8, 9); }, [](int p) { texture(p, 0.3f, 16, 0.5f, 0.05f, 0.0f); }},
    }, {
        {16, {{0, "x...x...x...x..."}, {1, "..o...o...o...o."}, {2, "....x.......x..."}, {3, "x.xxx.xxx.xxx.xx"},
              {4, "..x...x...x...x."}, {7, "...x..x....x..x."}, {9, "x..xx.x..x.xx..x"}}},
        {16, {{0, "x...x...x...x..."}, {1, "..o...o...o...o."}, {3, "xxxxxxxxxxxxxxxx"}, {5, "..x...x...x...x."},
              {6, ".x....x..x....x."}, {8, "......x.......x."}, {10, "x..x..x.x..x..x."}}},
    }, [] { setFx(0.5f, 0.15f, 0, 2, 0.45f, 0.4f, 0.65f, 0.55f); }});

    // ---------------------------------------------------------- Lo-Fi
    k.push_back({"Lo-Fi", "Soft dusty drums, jazzy Rhodes chords, vinyl", 82, 0.2f, {
        {"Kick", [] { Buf b = kick(130, 48, 22, 7, 0.2, 1.3, 0.5); lofi(b, 10, 2, 5000); normalize(b); return b; }, none},
        {"Snare", [] { Buf b = snare(180, 25, 12, 2400, 0.8, 0.35, 4); lofi(b, 9, 3, 6000); normalize(b, 0.8f); return b; }, [](int p) { verb(p, 0.2f); }},
        {"Hat", [] { Buf b = hat(0.03, 6000, 0.1, 0.3); lofi(b, 9, 2, 9000); return b; }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.6f); }},
        {"Open Hat", [] { Buf b = hat(0.2, 6000, 0.5, 0.3); lofi(b, 9, 2, 9000); return b; }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.55f); }},
        {"Rim", [] { Buf b = rim(1500); lofi(b, 8, 3, 5000); return b; }, none},
        {"Shaker", [] { Buf b = shaker(0.2, 0.04); lofi(b, 9, 2, 8000); return b; }, [](int p) { P(p, GP_P_GAIN, 0.55f); P(p, GP_P_PAN, 0.3f); }},
        {"Snap", [] { Buf b = snap(); lofi(b, 9, 3, 6000); return b; }, [](int p) { verb(p, 0.3f); }},
        {"Vinyl", [] { return vinyl(); }, [](int p) { P(p, GP_P_LOOP, 1); P(p, GP_P_ONESHOT, 0); P(p, GP_P_GAIN, 0.55f); }},
        {"Cmaj9", [] { Buf b = rhodes({48, 55, 59, 62, 64}); lofi(b, 11, 2, 4500, 0.002f); normalize(b, 0.8f); return b; }, [](int p) { verb(p, 0.3f); }},
        {"Am9", [] { Buf b = rhodes({45, 52, 55, 59, 60}); lofi(b, 11, 2, 4500, 0.002f); normalize(b, 0.8f); return b; }, [](int p) { verb(p, 0.3f); }},
        {"Dm9", [] { Buf b = rhodes({50, 57, 60, 64, 65}); lofi(b, 11, 2, 4500, 0.002f); normalize(b, 0.8f); return b; }, [](int p) { verb(p, 0.3f); }},
        {"G13", [] { Buf b = rhodes({43, 53, 57, 59, 64}); lofi(b, 11, 2, 4500, 0.002f); normalize(b, 0.8f); return b; }, [](int p) { verb(p, 0.3f); }},
        {"Bass C", [] { Buf b = synthBass(36, 2, 400, 300, 0.8, 2.5, 0.8); lofi(b, 12, 1, 2000, 0.0f); normalize(b, 0.85f); return b; }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Bass A", [] { Buf b = synthBass(33, 2, 400, 300, 0.8, 2.5, 0.8); lofi(b, 12, 1, 2000, 0.0f); normalize(b, 0.85f); return b; }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Bell", [] { Buf b = bell(76, 2.0, 2.5); lofi(b, 10, 2, 6000, 0.002f); return b; }, [](int p) { P(p, GP_P_DELAY_SEND, 0.35f); verb(p, 0.4f); }},
        {"Tape Pad", [] { Buf b = drone({48, 55, 59, 64}, 4.0, 1200, 0.3); lofi(b, 10, 2, 3500, 0.003f); normalize(b, 0.8f); return b; }, [](int p) { texture(p, 0.25f, 18, 0.3f, 0.1f, 0.1f); }},
    }, {
        {16, {{0, "x.......x.x....."}, {1, "....x.......x..."}, {2, "x.xgx.xgx.xgx.xg"}, {7, "x..............."},
              {8, "x..............."}, {9, "........x......."}, {12, "x.......o.....o."}}},
        {16, {{0, "x......x..x....."}, {1, "....x.......x..g"}, {2, "x.x.x.x.x.x.x.x."}, {4, "..o.......o....."}, {7, "x..............."},
              {10, "x..............."}, {11, "........x......."}, {13, "x.....o.o......."}}},
    }, [] { setFx(0.38f, 0.1f, 0.12f, 5, 0.35f, 0.35f, 0.55f, 0.6f); }});

    // ---------------------------------------------------------- Electro
    k.push_back({"Electro", "808-style electro: cowbell, toms, zaps, square bass", 118, 0.0f, {
        {"Kick", [] { return kick(150, 44, 18, 4.5, 0.3, 1.6, 0.9); }, none},
        {"Snare", [] { return snare(240, 30, 20, 5000, 1.1, 0.3, 6); }, [](int p) { verb(p, 0.25f); }},
        {"Clap", [] { return clap(1100, 16, 4, 0.01, 27); }, [](int p) { verb(p, 0.3f); }},
        {"Hat", [] { return hat(0.03, 7800, 0.1, 0.8, 31); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.6f); }},
        {"Open Hat", [] { return hat(0.25, 7800, 0.6, 0.8, 31); }, [](int p) { hatChoke(p); P(p, GP_P_GAIN, 0.55f); }},
        {"Cowbell", [] { return cowbell(); }, [](int p) { P(p, GP_P_GAIN, 0.6f); }},
        {"Tom Hi", [] { return tom(180, 9, 80); }, [](int p) { P(p, GP_P_PAN, 0.3f); }},
        {"Tom Lo", [] { return tom(110, 8, 60); }, [](int p) { P(p, GP_P_PAN, -0.3f); }},
        {"Conga", [] { return conga(260, 12); }, none},
        {"Zap", [] { return zap(); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.3f); }},
        {"Bass E", [] { return synthBass(40, 1, 500, 1500, 3.0, 5.0, 0.45); }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Bass A", [] { return synthBass(45, 1, 500, 1500, 3.0, 5.0, 0.45); }, [](int p) { P(p, GP_P_MONO, 1); }},
        {"Stab", [] { return chord({52, 59, 62, 67}, 1, 3000, 0.002, 7.0, 0.5); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.3f); }},
        {"Laser", [] { return zap(5000, 300, 12); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.4f); verb(p, 0.3f); }},
        {"Vox Ah", [] { return vox(52, 750, 1200, 2500, 0.8); }, [](int p) { verb(p, 0.3f); }},
        {"Crash", [] { return cymbal(2.2, 0.8, 4000, 37); }, [](int p) { P(p, GP_P_GAIN, 0.55f); }},
    }, {
        {16, {{0, "x.....x...x....."}, {1, "....x.......x..."}, {3, "x.x.x.x.x.x.x.x."}, {5, "..x....x..x....."},
              {10, "x..x..x...x.x..."}, {9, "..............x."}}},
        {16, {{0, "x.....x...x...x."}, {2, "....x.......x..."}, {3, "xxx.xxx.xxx.xxx."}, {6, "............x.x."},
              {7, ".............x.x"}, {11, "x..x..x...x.x..."}, {12, "..x.......x....."}}},
    }, [] { setFx(0.5f, 0.12f, 0.05f, 3, 0.4f, 0.6f, 0.4f, 0.45f); }});

    // ---------------------------------------------------------- Ambient
    k.push_back({"Ambient", "Hold the pads: granular drones, glass, choir, soft pulses", 75, 0.0f, {
        {"Pulse", [] { return kick(90, 45, 12, 5, 0.0, 1.0, 0.6); }, [](int p) { verb(p, 0.3f); }},
        {"Tick", [] { return rim(2600); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.5f); verb(p, 0.4f); P(p, GP_P_GAIN, 0.5f); }},
        {"Brush", [] { return shaker(0.4, 0.12, 14); }, [](int p) { verb(p, 0.5f); P(p, GP_P_GAIN, 0.55f); }},
        {"Felt", [] { return rhodes({60, 67, 74}, 3.0); }, [](int p) { verb(p, 0.6f); P(p, GP_P_DELAY_SEND, 0.3f); }},
        {"Drone Cm", [] { return drone({36, 43, 51, 55}, 5.0, 700, 0.6, 61); }, [](int p) { texture(p, 0.3f, 18, 0.4f, 0.06f, 0.0f); }},
        {"Drone Ab", [] { return drone({32, 44, 51, 56}, 5.0, 700, 0.6, 62); }, [](int p) { texture(p, 0.3f, 18, 0.4f, 0.06f, 0.0f); }},
        {"Glass", [] { return glass({72, 79, 84, 87}, 4.0); }, [](int p) { texture(p, 0.18f, 26, 0.6f, 0.15f, 0.2f); }},
        {"Choir", [] { return vox(60, 650, 1080, 2650, 3.0); }, [](int p) { texture(p, 0.25f, 20, 0.3f, 0.1f, 0.1f); }},
        {"Shimmer", [] { return glass({84, 91, 96}, 3.0); }, [](int p) { texture(p, 0.12f, 30, 0.8f, 0.4f, 12.0f); }},
        {"Wind", [] { return noiseSweep(4.0, true); }, [](int p) { texture(p, 0.35f, 12, 1.0f, 0.2f, 2.0f); }},
        {"Sub Swell", [] { return chord({24, 36}, 2, 300, 1.5, 0.3, 4.0); }, [](int p) { hold(p); P(p, GP_P_ATTACK, 0.8f); P(p, GP_P_RELEASE, 2.0f); }},
        {"Bells", [] { return bell(84, 1.41, 1.2); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.5f); verb(p, 0.6f); }},
        {"Bell Low", [] { return bell(72, 1.41, 1.0); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.5f); verb(p, 0.6f); }},
        {"Pluck", [] { return pluck(67, 2500, 2.0); }, [](int p) { P(p, GP_P_DELAY_SEND, 0.5f); verb(p, 0.5f); }},
        {"Freeze", [] { return rhodes({55, 62, 69, 74}, 3.0); }, [](int p) { texture(p, 0.4f, 24, 0.15f, 0.0f, 0.0f); }},
        {"Rain", [] { return vinyl(4.0); }, [](int p) { P(p, GP_P_LOOP, 1); P(p, GP_P_ONESHOT, 0); verb(p, 0.5f); P(p, GP_P_GAIN, 0.6f); }},
    }, {
        {16, {{0, "x.......x......."}, {1, "..x.....o...x..."}, {3, "x..............."}, {11, "......x.......o."}}},
        {16, {{0, "x.......x......."}, {2, "....x.......x..."}, {12, "x.....x...o....."}, {13, "...o......o..o.."}}},
    }, [] { setFx(0.5f, 0, 0, 6, 0.55f, 0.4f, 0.9f, 0.35f); }});

    return k;
}

void applyPattern(int index, const Pattern& pat) {
    gp_clear_pattern(index);
    gp_set_pattern_length(index, pat.length);
    for (auto& row : pat.rows) {
        const char* s = row.second;
        for (int i = 0; s[i] && i < GP_MAX_STEPS; ++i) {
            float v = s[i] == 'x' ? 1.0f : s[i] == 'o' ? 0.65f : s[i] == 'g' ? 0.35f : 0.0f;
            if (v > 0) gp_set_step(index, row.first, i, v);
        }
    }
}

void loadSound(const Sound& s, int pad) {
    Buf b = s.make();
    gp_reset_pad_params(pad);
    gp_load_pad_sample(pad, b.data(), nullptr, (int)b.size(), SR, 0, 0);
    s.tweak(pad);
}
} // namespace

int gp_kit_count(void) { return (int)kits().size(); }
const char* gp_kit_name(int k) { return (k >= 0 && k < gp_kit_count()) ? kits()[k].name : ""; }
const char* gp_kit_description(int k) { return (k >= 0 && k < gp_kit_count()) ? kits()[k].desc : ""; }
const char* gp_kit_pad_name(int k, int pad) {
    if (k < 0 || k >= gp_kit_count() || pad < 0 || pad >= (int)kits()[k].sounds.size()) return "";
    return kits()[k].sounds[pad].name;
}

void gp_load_kit(int k) {
    if (k < 0 || k >= gp_kit_count()) return;
    const Kit& kit = kits()[k];
    gp_set_playing(0);
    gp_all_notes_off();
    for (int p = 0; p < GP_NUM_PADS; ++p) {
        if (p < (int)kit.sounds.size()) loadSound(kit.sounds[p], p);
        else { gp_clear_pad_sample(p); gp_reset_pad_params(p); }
    }
    for (int a = 0; a < GP_NUM_PATTERNS; ++a) { gp_clear_pattern(a); gp_set_pattern_length(a, 16); }
    for (size_t i = 0; i < kit.patterns.size() && i < GP_NUM_PATTERNS; ++i) applyPattern((int)i, kit.patterns[i]);
    gp_set_master(GP_M_BPM, kit.bpm);
    gp_set_master(GP_M_SWING, kit.swing);
    gp_set_master(GP_M_REPEAT, 0);
    gp_set_master(GP_M_RESO, 0.2f);
    gp_set_master(GP_M_VOLUME, 0.68f);
    kit.fx();
    gp_select_pattern(0);
}

void gp_load_kit_sound(int k, int kitPad, int dst) {
    if (k < 0 || k >= gp_kit_count() || dst < 0 || dst >= GP_NUM_PADS) return;
    const Kit& kit = kits()[k];
    if (kitPad < 0 || kitPad >= (int)kit.sounds.size()) return;
    loadSound(kit.sounds[kitPad], dst);
}
