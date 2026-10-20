// Synthesized starter kit so the app is playable on first launch (no bundled audio).
#include "GPEngine.h"
#include "GPDSP.hpp"
#include <cstring>
#include <vector>

using namespace gp;

namespace {
const double SR = 48000.0;
using Buf = std::vector<float>;

Buf make(double sec) { return Buf((size_t)(sec * SR), 0.0f); }
double tt(size_t i) { return i / SR; }

struct Noise { Rng r{12345}; float operator()() { return r.bi(); } };

Buf kick() {
    Buf b = make(0.6); double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i), f = 46 + 140 * std::exp(-t * 28);
        ph += 2 * M_PI * f / SR;
        float body = (float)std::sin(ph) * (float)std::exp(-t * 6.5);
        float click = (float)std::exp(-t * 400) * 0.5f;
        b[i] = std::tanh((body + click) * 1.6f) * 0.95f;
    }
    return b;
}
Buf snare() {
    Buf b = make(0.35); Noise n; SVF bp; bp.set(3200, 0.8f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        float tone = (float)(std::sin(2 * M_PI * 185 * t) * 0.5 + std::sin(2 * M_PI * 330 * t) * 0.25) * (float)std::exp(-t * 28);
        float l, bb, h; bp.tick(n(), l, bb, h);
        float noise = (bb * 1.6f + h * 0.4f) * (float)std::exp(-t * 15);
        b[i] = std::tanh((tone + noise) * 1.4f) * 0.8f;
    }
    return b;
}
Buf hat(double decay, double len) {
    Buf b = make(len); Noise n; SVF hp; hp.set(7500, 0.9f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        // metallic: sum of square-ish partials + noise
        double t = tt(i); float m = 0;
        static const double fr[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
        for (double f : fr) m += std::sin(2 * M_PI * f * 8.0 * t) > 0 ? 1.0f : -1.0f;
        float x = m * 0.08f + n() * 0.5f;
        b[i] = hp.hp(x) * (float)std::exp(-t / decay) * 0.6f;
    }
    return b;
}
Buf clap() {
    Buf b = make(0.4); Noise n; SVF bp; bp.set(1400, 1.4f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        double e = 0;
        for (int k = 0; k < 3; ++k) { double tk = t - k * 0.011; if (tk >= 0) e += std::exp(-tk * 180); }
        double tail = t > 0.022 ? std::exp(-(t - 0.022) * 18) : 0;
        float l, bb, h; bp.tick(n(), l, bb, h);
        b[i] = bb * (float)(e * 0.8 + tail * 0.6) * 1.8f;
    }
    return b;
}
Buf rim() {
    Buf b = make(0.12);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        b[i] = (float)((std::sin(2 * M_PI * 1700 * t) * 0.6 + std::sin(2 * M_PI * 820 * t) * 0.4) * std::exp(-t * 60)) * 0.8f;
    }
    return b;
}
Buf tom() {
    Buf b = make(0.5); double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i), f = 110 + 60 * std::exp(-t * 20);
        ph += 2 * M_PI * f / SR;
        b[i] = (float)(std::sin(ph) * std::exp(-t * 7)) * 0.85f;
    }
    return b;
}
Buf shaker() {
    Buf b = make(0.18); Noise n; SVF hp; hp.set(5000, 0.7f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        double env = t < 0.03 ? t / 0.03 : std::exp(-(t - 0.03) * 30);
        b[i] = hp.hp(n()) * (float)env * 0.45f;
    }
    return b;
}
double midiHz(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }
Buf subBass() {
    Buf b = make(1.6);
    double f = midiHz(36);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        float x = (float)std::sin(2 * M_PI * f * t);
        b[i] = std::tanh(x * 1.8f) * 0.8f * (float)std::min(1.0, t * 400) * (float)std::exp(-t * 1.2);
    }
    return b;
}
float saw(double ph) { ph -= std::floor(ph); return (float)(2 * ph - 1); }
Buf pluck() {
    Buf b = make(0.8); SVF lp;
    double f = midiHz(48);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        if (i % 32 == 0) lp.set(200 + 3500 * (float)std::exp(-t * 12), 2.0f, (float)SR);
        float x = saw(f * t) + 0.5f * saw(f * 1.005 * t);
        b[i] = lp.lp(x) * (float)std::exp(-t * 3.5) * 0.6f;
    }
    return b;
}
Buf chord(const int* notes, int n, double len, double cutoff, double decay) {
    Buf b = make(len); SVF lp; lp.set((float)cutoff, 0.9f, (float)SR);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i); float x = 0;
        for (int k = 0; k < n; ++k) {
            double f = midiHz(notes[k]);
            x += saw(f * t) + saw(f * 1.004 * t + 0.3) * 0.7f;
        }
        double env = std::min(1.0, t * 200) * std::exp(-t * decay);
        b[i] = lp.lp(x / n) * (float)env * 0.55f;
    }
    return b;
}
Buf keys() {
    Buf b = make(1.8);
    static const int notes[4] = {60, 63, 67, 70};
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i); double x = 0;
        for (int n : notes) {
            double f = midiHz(n);
            double mod = std::sin(2 * M_PI * f * t) * 1.2 * std::exp(-t * 4);
            x += std::sin(2 * M_PI * f * t + mod);
        }
        b[i] = (float)(x / 4 * std::exp(-t * 1.6) * std::min(1.0, t * 300)) * 0.7f;
    }
    return b;
}
Buf vox() {
    Buf b = make(1.4);
    SVF f1, f2, f3; f1.set(800, 6, (float)SR); f2.set(1150, 6, (float)SR); f3.set(2900, 7, (float)SR);
    double f0 = midiHz(57);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        double vib = 1.0 + 0.006 * std::sin(2 * M_PI * 5.2 * t) * std::min(1.0, t * 2);
        float src = saw(f0 * vib * t);
        float l, bp1, bp2, bp3, h;
        f1.tick(src, l, bp1, h); f2.tick(src, l, bp2, h); f3.tick(src, l, bp3, h);
        double env = std::min(1.0, t * 12) * (t > 1.1 ? std::max(0.0, 1 - (t - 1.1) / 0.3) : 1.0);
        b[i] = (bp1 + bp2 * 0.6f + bp3 * 0.3f) * (float)env * 0.5f;
    }
    return b;
}
Buf riser() {
    Buf b = make(2.0); Noise n; SVF bp;
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        if (i % 32 == 0) bp.set((float)(300 * std::pow(30.0, t / 2.0)), 2.5f, (float)SR);
        float l, bb, h; bp.tick(n(), l, bb, h);
        double env = std::pow(t / 2.0, 1.5) * (t > 1.95 ? (2.0 - t) / 0.05 : 1.0);
        b[i] = bb * (float)env * 0.9f;
    }
    return b;
}
Buf bell() {
    Buf b = make(2.2);
    double f = midiHz(79);
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        double mod = std::sin(2 * M_PI * f * 3.5 * t) * 2.0 * std::exp(-t * 2.5);
        b[i] = (float)(std::sin(2 * M_PI * f * t + mod) * std::exp(-t * 2.2)) * 0.45f;
    }
    return b;
}
Buf drone() {
    // slow evolving chord with noise breath — food for texture mode
    Buf b = make(4.0); Noise n; SVF lp;
    static const int notes[5] = {48, 55, 62, 63, 70};
    for (size_t i = 0; i < b.size(); ++i) {
        double t = tt(i);
        if (i % 64 == 0) lp.set((float)(600 + 900 * (0.5 + 0.5 * std::sin(2 * M_PI * 0.25 * t))), 1.2f, (float)SR);
        float x = 0;
        for (int k = 0; k < 5; ++k) { double f = midiHz(notes[k]); x += saw(f * t + k * 0.13) + saw(f * 1.003 * t); }
        x = x / 10 + n() * 0.03f;
        double env = std::min(1.0, t * 1.5) * std::min(1.0, (4.0 - t) * 2);
        b[i] = lp.lp(x) * (float)env * 0.8f;
    }
    return b;
}
} // namespace

static const char* kKitNames[16] = {"Kick", "Snare", "Hat", "Open Hat", "Clap", "Rim", "Tom", "Shaker",
                                   "Sub", "Pluck", "Stab", "Keys", "Vox", "Riser", "Bell", "Drone"};
const char* gp_starter_kit_name(int pad) { return (pad >= 0 && pad < 16) ? kKitNames[pad] : ""; }

void gp_load_starter_kit(void) {
    struct Item { const char* name; Buf audio; };
    static const int stab[4] = {60, 63, 67, 74};
    std::vector<Item> kit;
    kit.push_back({"Kick", kick()});
    kit.push_back({"Snare", snare()});
    kit.push_back({"Hat", hat(0.035, 0.12)});
    kit.push_back({"Open Hat", hat(0.22, 0.6)});
    kit.push_back({"Clap", clap()});
    kit.push_back({"Rim", rim()});
    kit.push_back({"Tom", tom()});
    kit.push_back({"Shaker", shaker()});
    kit.push_back({"Sub", subBass()});
    kit.push_back({"Pluck", pluck()});
    kit.push_back({"Stab", chord(stab, 4, 0.9, 2400, 4.0)});
    kit.push_back({"Keys", keys()});
    kit.push_back({"Vox", vox()});
    kit.push_back({"Riser", riser()});
    kit.push_back({"Bell", bell()});
    kit.push_back({"Drone", drone()});

    for (int p = 0; p < 16; ++p) {
        gp_reset_pad_params(p);
        gp_load_pad_sample(p, kit[p].audio.data(), nullptr, (int)kit[p].audio.size(), SR, 0, 1);
    }
    gp_set_pad_param(2, GP_P_CHOKE, 1); gp_set_pad_param(3, GP_P_CHOKE, 1);     // hats choke
    gp_set_pad_param(2, GP_P_GAIN, 0.7f);
    gp_set_pad_param(3, GP_P_GAIN, 0.65f);
    gp_set_pad_param(7, GP_P_PAN, 0.35f); gp_set_pad_param(7, GP_P_GAIN, 0.65f);
    gp_set_pad_param(5, GP_P_PAN, -0.3f);
    gp_set_pad_param(4, GP_P_REVERB_SEND, 0.25f);
    gp_set_pad_param(8, GP_P_MONO, 1); gp_set_pad_param(8, GP_P_ONESHOT, 0); gp_set_pad_param(8, GP_P_RELEASE, 0.08f);
    gp_set_pad_param(9, GP_P_DELAY_SEND, 0.3f);
    gp_set_pad_param(10, GP_P_REVERB_SEND, 0.35f); gp_set_pad_param(10, GP_P_DELAY_SEND, 0.2f);
    gp_set_pad_param(11, GP_P_REVERB_SEND, 0.3f);
    gp_set_pad_param(12, GP_P_REVERB_SEND, 0.4f); gp_set_pad_param(12, GP_P_ONESHOT, 0); gp_set_pad_param(12, GP_P_RELEASE, 0.3f);
    gp_set_pad_param(14, GP_P_DELAY_SEND, 0.4f); gp_set_pad_param(14, GP_P_REVERB_SEND, 0.4f);
    // pad 15: texture mode drone, hold to play
    gp_set_pad_param(15, GP_P_MODE, 1); gp_set_pad_param(15, GP_P_ONESHOT, 0);
    gp_set_pad_param(15, GP_P_ATTACK, 0.4f); gp_set_pad_param(15, GP_P_RELEASE, 1.5f);
    gp_set_pad_param(15, GP_P_GRAIN_SIZE, 0.2f); gp_set_pad_param(15, GP_P_GRAIN_DENSITY, 22);
    gp_set_pad_param(15, GP_P_GRAIN_SPRAY, 0.35f); gp_set_pad_param(15, GP_P_GRAIN_SCAN, 0.08f);
    gp_set_pad_param(15, GP_P_GRAIN_PITCHRND, 0.15f); gp_set_pad_param(15, GP_P_REVERB_SEND, 0.5f);

    // demo beat (boom bap, 92 BPM)
    for (int a = 0; a < GP_NUM_PATTERNS; ++a) { gp_clear_pattern(a); gp_set_pattern_length(a, 16); }
    auto st = [](int pad, std::initializer_list<int> steps, float v) { for (int s : steps) gp_set_step(0, pad, s, v); };
    st(0, {0, 7, 10}, 1.0f);
    st(1, {4, 12}, 1.0f);
    st(2, {0, 2, 4, 6, 8, 10, 12}, 0.8f); st(2, {3, 11}, 0.4f);
    st(3, {14}, 0.7f);
    st(4, {12}, 0.6f);
    st(8, {0, 10}, 0.9f);
    st(10, {0}, 0.7f);
    gp_set_master(GP_M_BPM, 92.0f);
    gp_set_master(GP_M_SWING, 0.12f);
    gp_select_pattern(0);
}
