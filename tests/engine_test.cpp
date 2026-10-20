// Engine tests — build: g++ -std=c++17 -O2 -I../GrainPad/Engine engine_test.cpp ../GrainPad/Engine/*.cpp
#include "GPEngine.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } else { std::printf("ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double SR = 48000.0;
static const int BLOCK = 256;

struct Render { std::vector<float> L, R; };

static Render render(int frames) {
    Render r; r.L.resize(frames); r.R.resize(frames);
    for (int i = 0; i < frames; i += BLOCK) {
        int n = std::min(BLOCK, frames - i);
        gp_render(&r.L[i], &r.R[i], n);
    }
    return r;
}

static bool finite(const Render& r) {
    for (size_t i = 0; i < r.L.size(); ++i) { if (!std::isfinite(r.L[i]) || !std::isfinite(r.R[i])) return false; }
    return true;
}
static float peak(const Render& r, int a = 0, int b = -1) {
    if (b < 0) b = (int)r.L.size();
    float p = 0; for (int i = a; i < b; ++i) p = std::max(p, std::max(std::fabs(r.L[i]), std::fabs(r.R[i]))); return p;
}
static float rms(const Render& r, int a, int b) {
    double s = 0; for (int i = a; i < b; ++i) s += r.L[i] * r.L[i]; return (float)std::sqrt(s / std::max(1, b - a));
}
static int firstAbove(const Render& r, float thr, int from = 0) {
    for (int i = from; i < (int)r.L.size(); ++i) if (std::fabs(r.L[i]) > thr) return i; return -1;
}

// Synthetic drum hits
static void addKick(std::vector<float>& b, int at, double sr) {
    for (int i = 0; i < (int)(sr * 0.25) && at + i < (int)b.size(); ++i) {
        double t = i / sr; double f = 50 + 120 * std::exp(-t * 30);
        b[at + i] += (float)(0.9 * std::sin(2 * M_PI * f * t) * std::exp(-t * 14));
    }
}
static void addSnare(std::vector<float>& b, int at, double sr, unsigned& seed) {
    for (int i = 0; i < (int)(sr * 0.18) && at + i < (int)b.size(); ++i) {
        seed = seed * 1664525u + 1013904223u; float n = ((seed >> 9) / 4194304.0f) - 1.0f;
        double t = i / sr;
        b[at + i] += (float)((0.6 * n + 0.3 * std::sin(2 * M_PI * 190 * t)) * std::exp(-t * 22));
    }
}
static void addHat(std::vector<float>& b, int at, double sr, unsigned& seed) {
    float prev = 0;
    for (int i = 0; i < (int)(sr * 0.04) && at + i < (int)b.size(); ++i) {
        seed = seed * 1664525u + 1013904223u; float n = ((seed >> 9) / 4194304.0f) - 1.0f;
        float hp = n - prev; prev = n;
        b[at + i] += (float)(0.25 * hp * std::exp(-(i / sr) * 90));
    }
}
// 2 bars at the given tempo: kick 1 & 3, snare 2 & 4, hats on 8ths
static std::vector<float> makeLoop(double bpm, double sr) {
    double beat = sr * 60.0 / bpm;
    int frames = (int)std::round(beat * 8);
    std::vector<float> b(frames, 0.0f);
    unsigned seed = 7;
    for (int k = 0; k < 8; ++k) {
        int at = (int)std::round(k * beat);
        if (k % 2 == 0) addKick(b, at, sr); else addSnare(b, at, sr, seed);
        addHat(b, at, sr, seed);
        addHat(b, (int)std::round(at + beat / 2), sr, seed);
    }
    return b;
}
static std::vector<float> makeTone(double hz, double seconds, double sr) {
    std::vector<float> b((size_t)(seconds * sr));
    for (size_t i = 0; i < b.size(); ++i) b[i] = (float)(0.5 * std::sin(2 * M_PI * hz * i / sr));
    return b;
}
static void resetAll() {
    gp_set_playing(0); gp_set_recording(0); gp_all_notes_off();
    for (int p = 0; p < GP_NUM_PADS; ++p) gp_reset_pad_params(p);
    for (int a = 0; a < GP_NUM_PATTERNS; ++a) { gp_clear_pattern(a); gp_set_pattern_length(a, 16); }
    gp_set_master(GP_M_DRIVE, 0); gp_set_master(GP_M_CRUSH, 0); gp_set_master(GP_M_FILTER, 0.5f);
    gp_set_master(GP_M_REPEAT, 0); gp_set_master(GP_M_SWING, 0); gp_set_master(GP_M_METRONOME, 0);
    gp_select_pattern(0);
    render(SR * 0.3);
    gp_collect_garbage();
}

int main() {
    gp_prepare(SR, 512);

    // ---------------------------------------------------------------- basics
    {
        auto tone = makeTone(440, 1.0, SR);
        int kept = gp_load_pad_sample(0, tone.data(), nullptr, (int)tone.size(), SR, 0, 0);
        CHECK(kept == (int)tone.size(), "sample loads (%d frames)", kept);
        render(BLOCK);
        gp_collect_garbage();
        gp_trigger(0, 1.0f);
        auto r = render(SR * 0.5);
        CHECK(finite(r), "output finite");
        CHECK(peak(r) > 0.2f && peak(r) <= 1.0f, "triggered pad is audible, peak %.3f", peak(r));
        int on = firstAbove(r, 0.01f);
        CHECK(on >= 0 && on < BLOCK * 2, "trigger latency %d samples", on);
        CHECK(gp_pad_hit_counter(0) == 1, "hit counter increments");
    }
    resetAll();

    // ------------------------------------------------- trim + normalize
    {
        std::vector<float> s(SR * 1.0, 0.0f);
        for (int i = 12000; i < 24000; ++i) s[i] = 0.1f * std::sin(i * 0.05f);
        int kept = gp_load_pad_sample(1, s.data(), nullptr, (int)s.size(), SR, 1, 1);
        CHECK(kept < 16000 && kept > 12000, "auto-trim removes silence (%d frames)", kept);
        std::vector<float> pk(64 * 2);
        gp_pad_peaks(1, pk.data(), 64);
        float mx = 0; for (float v : pk) mx = std::max(mx, std::fabs(v));
        CHECK(mx > 0.8f && mx < 0.95f, "normalized to about -1 dBFS (%.3f)", mx);
    }
    resetAll();

    // ------------------------------------------------- sequencer timing
    {
        auto click = std::vector<float>(2000, 0.0f);
        for (int i = 0; i < 2000; ++i) click[i] = 0.8f * std::exp(-i / 200.0f) * (i % 2 ? 1 : -1);
        gp_load_pad_sample(2, click.data(), nullptr, (int)click.size(), SR, 0, 0);
        gp_set_master(GP_M_BPM, 120.0f);              // step = 6000 samples @ 48k
        gp_set_step(0, 2, 0, 1.0f);
        gp_set_step(0, 2, 4, 1.0f);
        gp_set_step(0, 2, 8, 1.0f);
        render(BLOCK); gp_collect_garbage();
        gp_set_playing(1);
        auto r = render(SR * 2.2);
        int a = firstAbove(r, 0.05f);
        int b = firstAbove(r, 0.05f, a + 3000);
        int c = firstAbove(r, 0.05f, b + 3000);
        CHECK(a >= 0 && a < BLOCK, "first step fires at block start (%d)", a);
        CHECK(std::abs((b - a) - 24000) <= 2, "step 4 is exactly one beat later (%d)", b - a);
        CHECK(std::abs((c - b) - 24000) <= 2, "step 8 is exactly one beat later (%d)", c - b);
        int d = firstAbove(r, 0.05f, c + 3000);
        CHECK(std::abs((d - a) - 96000) <= 2, "pattern loops after 16 steps (%d)", d - a);
        gp_set_playing(0); render(BLOCK);

        // swing delays odd steps only
        gp_clear_pattern(0);
        gp_set_step(0, 2, 0, 1.0f); gp_set_step(0, 2, 1, 1.0f); gp_set_step(0, 2, 2, 1.0f);
        gp_set_master(GP_M_SWING, 0.5f);
        render(SR * 0.2);
        gp_set_playing(1);
        r = render(SR * 0.6);
        a = firstAbove(r, 0.05f); b = firstAbove(r, 0.05f, a + 2500); c = firstAbove(r, 0.05f, b + 2500);
        CHECK(std::abs((b - a) - 9000) <= 2 && std::abs((c - a) - 12000) <= 2, "swing 50%% shifts odd steps (%d, %d)", b - a, c - a);
        gp_set_playing(0); gp_set_master(GP_M_SWING, 0); render(BLOCK);
    }
    resetAll();

    // ------------------------------------------------- live record quantize
    {
        auto click = std::vector<float>(800, 0.5f);
        gp_load_pad_sample(3, click.data(), nullptr, 800, SR, 0, 0);
        gp_set_master(GP_M_BPM, 120.0f);
        render(BLOCK); gp_collect_garbage();
        gp_set_recording(1);
        gp_set_playing(1);
        render(6000 * 2 + 4000);                       // inside step 2, past its middle
        gp_trigger(3, 1.0f);
        render(BLOCK * 2);
        CHECK(gp_get_step(0, 3, 3) > 0.9f && gp_get_step(0, 3, 2) == 0, "late hit quantizes to the next step");
        render(25000 - (6000 * 2 + 4000 + BLOCK * 2));  // ~1000 samples into step 4
        gp_trigger(3, 0.5f);
        render(BLOCK * 2);
        CHECK(gp_get_step(0, 3, 4) > 0.3f && gp_get_step(0, 3, 5) == 0, "early hit quantizes to the current step");
        gp_set_playing(0); render(BLOCK);
    }
    resetAll();

    // ------------------------------------------------- tempo detection
    for (double bpm : {90.0, 120.0, 140.0, 174.0}) {
        auto loop = makeLoop(bpm, SR);
        gp_load_pad_sample(4, loop.data(), nullptr, (int)loop.size(), SR, 0, 0);
        float d = gp_detect_bpm(4);
        CHECK(std::fabs(d - bpm) < 1.0f, "detects %.0f BPM loop -> %.1f", bpm, d);
    }
    {
        auto tone = makeTone(220, 3.0, SR);
        gp_load_pad_sample(4, tone.data(), nullptr, (int)tone.size(), SR, 0, 0);
        float d = gp_detect_bpm(4);
        CHECK(d == 0.0f, "steady tone has no tempo (%.1f)", d);
    }
    resetAll();

    // ------------------------------------------------- smart chop
    {
        auto loop = makeLoop(100.0, SR);   // 8 beats, 16 eighth-note hats → many transients
        gp_load_pad_sample(0, loop.data(), nullptr, (int)loop.size(), SR, 0, 0);
        int n = gp_smart_chop(0, 0, 16, 0, 1);
        CHECK(n >= 12 && n <= 16, "transient chop finds the hits (%d slices)", n);
        CHECK(gp_pad_sample_id(5) == gp_pad_sample_id(0), "slices share the audio");
        float a5 = gp_get_pad_param(5, GP_P_START), e4 = gp_get_pad_param(4, GP_P_END);
        CHECK(std::fabs(a5 - e4) < 1e-6f, "slices are contiguous");
        CHECK(gp_get_pattern_length(1) == 32, "pattern length = loop length (%d)", gp_get_pattern_length(1));
        int hits = 0; for (int p = 0; p < 16; ++p) for (int s = 0; s < 32; ++s) if (gp_get_step(1, p, s) > 0) ++hits;
        CHECK(hits == n, "pattern replays every slice (%d)", hits);
        CHECK(std::fabs(gp_get_pad_param(3, GP_P_SRC_BPM) - 100.0f) < 1.0f, "slices know their tempo");

        // play the chopped loop at the project tempo: should not blow up
        gp_set_master(GP_M_BPM, 100.0f);
        render(BLOCK); gp_collect_garbage();
        gp_select_pattern(1);
        render(BLOCK);
        gp_set_playing(1);
        auto r = render(SR * 5);
        CHECK(finite(r) && peak(r) > 0.3f, "chopped loop plays back (peak %.2f)", peak(r));
        gp_set_playing(0); render(BLOCK);

        int eq = gp_smart_chop(0, 0, 8, 1, -1);
        CHECK(eq == 8, "equal chop gives 8 slices");
    }
    resetAll();

    // ------------------------------------------------- variations
    {
        auto loop = makeLoop(100.0, SR);
        gp_load_pad_sample(0, loop.data(), nullptr, (int)loop.size(), SR, 0, 0);
        gp_smart_chop(0, 0, 16, 0, 0);
        std::vector<float> src(GP_NUM_PADS * GP_MAX_STEPS), v0(src.size()), v1(src.size()), v1b(src.size()), v2(src.size());
        gp_read_steps(0, src.data());
        gp_make_variation(0, 1, 0.0f, v0.data());
        gp_make_variation(0, 1, 0.7f, v1.data());
        gp_make_variation(0, 1, 0.7f, v1b.data());
        gp_make_variation(0, 2, 0.7f, v2.data());
        auto diff = [&](const std::vector<float>& a, const std::vector<float>& b) {
            int d = 0; for (size_t i = 0; i < a.size(); ++i) if ((a[i] > 0) != (b[i] > 0)) ++d; return d; };
        CHECK(diff(src, v0) == 0, "amount 0 keeps the pattern");
        CHECK(diff(src, v1) >= 4, "variation changes the pattern (%d cells)", diff(src, v1));
        CHECK(diff(v1, v1b) == 0, "same seed = same variation");
        CHECK(diff(v1, v2) > 0, "different seed = different variation");
        int beyond = 0; int len = gp_get_pattern_length(0);
        for (int p = 0; p < 16; ++p) for (int s = len; s < GP_MAX_STEPS; ++s) if (v1[p * GP_MAX_STEPS + s] > 0) ++beyond;
        CHECK(beyond == 0, "variation stays inside the pattern length");
        gp_apply_steps(0, v1.data());
        std::vector<float> back(src.size()); gp_read_steps(0, back.data());
        CHECK(diff(back, v1) == 0, "apply/read round-trip");
    }
    resetAll();

    // ------------------------------------------------- tempo sync
    {
        auto tone = makeTone(300, 2.0, SR);
        for (int i = (int)tone.size() - 10; i < (int)tone.size(); ++i) tone[i] = 0;
        gp_load_pad_sample(6, tone.data(), nullptr, (int)tone.size(), SR, 0, 0);
        gp_set_master(GP_M_BPM, 120.0f);
        gp_set_pad_param(6, GP_P_SRC_BPM, 100.0f);
        gp_set_pad_param(6, GP_P_SYNC, 1.0f);
        render(BLOCK); gp_collect_garbage();
        gp_trigger(6, 1.0f);
        auto r = render(SR * 2.5);
        int end = 0; for (int i = (int)r.L.size() - 1; i >= 0; --i) if (std::fabs(r.L[i]) > 0.01f) { end = i; break; }
        CHECK(std::fabs(end - SR * 2.0 * 100.0 / 120.0) < SR * 0.02, "repitch sync shortens 100->120 BPM (%.3f s)", end / SR);

        gp_set_pad_param(6, GP_P_SYNC, 2.0f);
        gp_set_pad_param(6, GP_P_LOOP, 0.0f);
        render(BLOCK);
        gp_trigger(6, 1.0f);
        r = render(SR * 1.0);
        CHECK(finite(r) && rms(r, (int)(SR * 0.3), (int)(SR * 0.9)) > 0.05f, "stretch sync keeps sounding (rms %.3f)", rms(r, (int)(SR * 0.3), (int)(SR * 0.9)));
        gp_all_notes_off(); render(SR * 0.1);
    }
    resetAll();

    // ------------------------------------------------- texture mode
    {
        auto loop = makeLoop(120.0, SR);
        gp_load_pad_sample(7, loop.data(), nullptr, (int)loop.size(), SR, 0, 0);
        gp_set_pad_param(7, GP_P_MODE, 1.0f);
        gp_set_pad_param(7, GP_P_ONESHOT, 0.0f);
        gp_set_pad_param(7, GP_P_GRAIN_SCAN, 0.0f);
        gp_set_pad_param(7, GP_P_GRAIN_PITCHRND, 7.0f);
        gp_set_pad_param(7, GP_P_GRAIN_SPRAY, 0.5f);
        render(BLOCK); gp_collect_garbage();
        gp_trigger(7, 1.0f);
        auto r = render(SR * 4.0);
        CHECK(finite(r), "texture output finite");
        float a = rms(r, (int)(SR * 1.0), (int)(SR * 2.0)), b = rms(r, (int)(SR * 3.0), (int)(SR * 4.0));
        CHECK(a > 0.01f && b > 0.01f, "held texture sustains (%.3f, %.3f)", a, b);
        CHECK(peak(r) <= 1.0f, "texture peak bounded (%.2f)", peak(r));
        gp_release(7);
        r = render(SR * 1.0);
        CHECK(rms(r, (int)(SR * 0.5), (int)(SR * 1.0)) < 0.001f, "texture stops after release");
    }
    resetAll();

    // ------------------------------------------------- fx torture
    {
        auto loop = makeLoop(120.0, SR);
        for (int p = 0; p < 4; ++p) {
            gp_load_pad_sample(p, loop.data(), nullptr, (int)loop.size(), SR, 0, 0);
            gp_set_pad_param(p, GP_P_DELAY_SEND, 1.0f);
            gp_set_pad_param(p, GP_P_REVERB_SEND, 1.0f);
            gp_set_pad_param(p, GP_P_CUTOFF, 0.3f);
            gp_set_pad_param(p, GP_P_RESO, 1.0f);
            gp_set_pad_param(p, GP_P_GAIN, 1.0f);
        }
        gp_set_master(GP_M_DELAY_FB, 0.95f);
        gp_set_master(GP_M_REVERB_SIZE, 1.0f);
        gp_set_master(GP_M_DRIVE, 1.0f);
        gp_set_master(GP_M_CRUSH, 0.8f);
        gp_set_master(GP_M_RESO, 1.0f);
        gp_set_master(GP_M_VOLUME, 1.0f);
        render(BLOCK); gp_collect_garbage();
        for (int p = 0; p < 4; ++p) gp_trigger(p, 1.0f);
        Render all; all.L.reserve(SR * 12); all.R.reserve(SR * 12);
        for (int k = 0; k < 24; ++k) {
            gp_set_master(GP_M_FILTER, (float)(k % 12) / 11.0f);
            gp_set_master(GP_M_REPEAT, (float)((k / 3) % 5));
            if (k % 4 == 0) for (int p = 0; p < 4; ++p) gp_trigger(p, 1.0f);
            auto r = render(SR * 0.5);
            all.L.insert(all.L.end(), r.L.begin(), r.L.end());
            all.R.insert(all.R.end(), r.R.begin(), r.R.end());
        }
        CHECK(finite(all), "fx torture stays finite");
        CHECK(peak(all) <= 1.0f, "limiter holds the output under 0 dBFS (%.3f)", peak(all));
        float tail = rms(all, (int)all.L.size() - (int)SR, (int)all.L.size());
        CHECK(tail < 0.9f, "feedback does not run away (tail rms %.3f)", tail);
        gp_set_master(GP_M_REPEAT, 0); gp_set_master(GP_M_DRIVE, 0); gp_set_master(GP_M_CRUSH, 0);
        gp_set_master(GP_M_DELAY_FB, 0.35f); gp_set_master(GP_M_VOLUME, 0.8f);
    }
    resetAll();

    // ------------------------------------------------- polyphony / choke
    {
        auto tone = makeTone(200, 2.0, SR);
        gp_load_pad_sample(8, tone.data(), nullptr, (int)tone.size(), SR, 0, 0);
        render(BLOCK); gp_collect_garbage();
        for (int i = 0; i < 80; ++i) gp_trigger(8, 0.3f);   // more than voices
        auto r = render(SR * 0.5);
        CHECK(finite(r) && peak(r) <= 1.0f, "voice stealing is safe");
        gp_all_notes_off(); render(SR * 0.05);
        gp_load_pad_sample(9, tone.data(), nullptr, (int)tone.size(), SR, 0, 0);
        gp_set_pad_param(8, GP_P_CHOKE, 1); gp_set_pad_param(9, GP_P_CHOKE, 1);
        render(BLOCK); gp_collect_garbage();
        gp_trigger(8, 1.0f); render(SR * 0.2);
        gp_trigger(9, 1.0f); render(SR * 0.05);
        CHECK(gp_pad_playhead(8) < 0 && gp_pad_playhead(9) >= 0, "choke group cuts the other pad");
    }
    resetAll();

    // ------------------------------------------------- pattern queue
    {
        gp_set_master(GP_M_BPM, 240.0f);
        gp_set_pattern_length(0, 4);
        gp_set_playing(1);
        render(BLOCK * 4);
        gp_select_pattern(2);
        render(BLOCK);
        CHECK(gp_current_pattern() == 0 && gp_queued_pattern() == 2, "pattern change is queued while playing");
        render(SR * 0.3);
        CHECK(gp_current_pattern() == 2, "queued pattern starts at the bar end");
        gp_set_playing(0);
    }

    resetAll();
    // ------------------------------------------------- starter kit
    {
        gp_load_starter_kit();
        const char* names[16]; for (int p = 0; p < 16; ++p) names[p] = gp_starter_kit_name(p);
        int loaded = 0; for (int p = 0; p < 16; ++p) loaded += gp_pad_has_sample(p);
        CHECK(loaded == 16 && std::string(names[0]) == "Kick", "starter kit fills all pads");
        render(BLOCK); gp_collect_garbage();
        gp_set_playing(1);
        auto r = render(SR * 6);
        CHECK(finite(r) && peak(r) > 0.3f && peak(r) <= 1.0f, "demo beat plays (peak %.2f)", peak(r));
        gp_set_playing(0); render(BLOCK);
        for (int p = 0; p < 16; ++p) {
            gp_trigger(p, 1.0f);
            auto x = render(SR * 0.4);
            gp_release(p);
            render(SR * 0.6);
            if (!(finite(x) && peak(x) > 0.05f)) CHECK(false, "kit pad %d (%s) audible: %.3f", p, names[p], peak(x));
        }
        CHECK(true, "every kit pad checked");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
