// GrainPad real-time engine: voices (classic + texture), sequencer, master FX.
#include "GPEngine.h"
#include "GPInternal.hpp"
#include "GPDSP.hpp"

#include <atomic>
#include <array>
#include <cstring>
#include <deque>

namespace gp {

// ---------------------------------------------------------------------------
// Lock-free single-producer / single-consumer queue
template <typename T, int N>
struct Spsc {
    T buf[N];
    std::atomic<int> head{0}, tail{0};
    bool push(const T& v) {
        int h = head.load(std::memory_order_relaxed);
        int n = (h + 1) % N;
        if (n == tail.load(std::memory_order_acquire)) return false;
        buf[h] = v;
        head.store(n, std::memory_order_release);
        return true;
    }
    bool pop(T& v) {
        int t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        v = buf[t];
        tail.store((t + 1) % N, std::memory_order_release);
        return true;
    }
};

enum CmdType : uint8_t { C_TRIGGER, C_RELEASE, C_ALL_OFF, C_PLAY, C_STOP, C_RECORD, C_PATTERN, C_SAMPLE };
struct Cmd {
    CmdType type = C_TRIGGER;
    int pad = 0;
    float value = 0;
    Sample* sample = nullptr;
    uint32_t seq = 0;
};

// ---------------------------------------------------------------------------
static const float kPadDefaults[GP_PAD_PARAM_COUNT] = {
    0.8f,   // gain
    0.0f,   // pan
    0.0f,   // pitch
    0.0f,   // fine
    0.0f,   // start
    1.0f,   // end
    0.0f,   // attack
    0.3f,   // decay
    1.0f,   // sustain
    0.05f,  // release
    1.0f,   // cutoff
    0.1f,   // reso
    0.0f,   // reverse
    0.0f,   // loop
    1.0f,   // oneshot
    0.0f,   // mono
    0.0f,   // choke
    0.0f,   // mode
    0.12f,  // grain size
    18.0f,  // grain density
    0.15f,  // grain spray
    0.25f,  // grain scan
    0.0f,   // grain pitch rnd
    0.6f,   // grain stereo
    0.0f,   // delay send
    0.0f,   // reverb send
    0.0f,   // mute
    0.0f,   // src bpm
    0.0f,   // sync
};

static const float kMasterDefaults[GP_M_COUNT] = {
    0.68f, 0.5f, 0.2f, 0.0f, 0.0f, 3.0f, 0.35f, 0.5f, 0.5f, 0.4f, 0.0f, 92.0f, 0.0f, 1.0f,
};

static const float kDelaySteps[8] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
static const float kRepeatSteps[5] = {0.0f, 4.0f, 2.0f, 1.0f, 0.5f};

constexpr int kVoices = 32;
constexpr int kGrains = 16;
constexpr int kHann = 1024;

struct PadSnap {
    float p[GP_PAD_PARAM_COUNT];
};

struct Grain {
    bool on = false;
    double pos = 0, inc = 1;
    int len = 1, age = 0;
    float gl = 1, gr = 1;
};

struct Voice {
    bool active = false;
    int pad = -1;
    const Sample* s = nullptr;
    uint64_t order = 0;
    float vel = 1;
    // classic playback
    double pos = 0;
    // envelope
    int stage = 0;          // 0 attack, 1 decay/sustain, 2 release
    float env = 0;
    bool gate = true;
    int autoOff = -1;       // samples until automatic note-off (sequencer)
    bool killing = false;
    float killGain = 1;
    float declick = 0;
    // filter
    SVF fL, fR;
    // texture
    Grain grains[kGrains];
    double scan = 0, scanned = 0;
    float nextGrain = 0;
    int age = 0;
    Rng rng;
};

struct Engine {
    double sr = 48000.0;
    int maxFrames = 1024;
    bool prepared = false;

    std::atomic<float> padParams[GP_NUM_PADS][GP_PAD_PARAM_COUNT];
    std::atomic<float> master[GP_M_COUNT];
    std::atomic<uint8_t> steps[GP_NUM_PATTERNS][GP_NUM_PADS][GP_MAX_STEPS];
    std::atomic<int> patternLen[GP_NUM_PATTERNS];

    Spsc<Cmd, 4096> cmds;
    Spsc<uint32_t, 4096> acks;

    // audio-thread state
    const Sample* padSample[GP_NUM_PADS] = {};
    Voice voices[kVoices];
    uint64_t voiceOrder = 0;
    PadSnap snap[GP_NUM_PADS];
    float hann[kHann + 1];

    bool playing = false, recording = false;
    int curPattern = 0, queued = -1, curStep = -1;
    double toNext = 0, interval = 1;
    int skipStep[GP_NUM_PADS];

    // metronome
    int clickLeft = 0; float clickPhase = 0, clickFreq = 1000;

    // buses
    std::vector<float> dryL, dryR, dlyL, dlyR, revL, revR;

    // master fx
    SVF mFiltL, mFiltR; Smoother filterS; int filterCount = 0; bool filterOn = false; bool filterHP = false;
    DelayLine delL, delR; OnePole toneL, toneR; Smoother delayTime;
    Reverb reverb;
    float crushHold[2] = {0, 0}; float crushCount = 0;
    float limGain = 1;
    // repeat / stutter
    std::vector<float> repL, repR; int repW = 0; bool repActive = false; int repStart = 0, repLen = 1, repR_ = 0;
    Smoother repMix;
    Smoother volumeS;

    // UI feedback
    std::atomic<float> padActivity[GP_NUM_PADS];
    std::atomic<float> padPlayhead[GP_NUM_PADS];
    std::atomic<unsigned> padHits[GP_NUM_PADS];
    std::atomic<float> outPeak[2];
    std::atomic<int> aPlaying{0}, aRecording{0}, aStep{-1}, aPattern{0}, aQueued{-1};

    // main-thread registry
    std::shared_ptr<Sample> registry[GP_NUM_PADS];
    std::deque<std::pair<uint32_t, std::shared_ptr<Sample>>> pending;
    uint32_t nextSeq = 1;
    long nextSampleId = 1;

    Engine() {
        for (int p = 0; p < GP_NUM_PADS; ++p) {
            for (int i = 0; i < GP_PAD_PARAM_COUNT; ++i) padParams[p][i].store(kPadDefaults[i]);
            padActivity[p].store(0); padPlayhead[p].store(-1); padHits[p].store(0);
            skipStep[p] = -1;
        }
        for (int i = 0; i < GP_M_COUNT; ++i) master[i].store(kMasterDefaults[i]);
        for (int a = 0; a < GP_NUM_PATTERNS; ++a) {
            patternLen[a].store(16);
            for (int p = 0; p < GP_NUM_PADS; ++p)
                for (int s = 0; s < GP_MAX_STEPS; ++s) steps[a][p][s].store(0);
        }
        outPeak[0].store(0); outPeak[1].store(0);
        for (int i = 0; i <= kHann; ++i) { float x = std::sin(kPi * i / kHann); hann[i] = x * x; }
    }

    // -----------------------------------------------------------------------
    void prepare(double rate, int frames) {
        sr = rate > 1000 ? rate : 48000.0;
        maxFrames = std::max(frames, 64);
        for (auto* v : {&dryL, &dryR, &dlyL, &dlyR, &revL, &revR}) v->assign(maxFrames, 0.0f);
        delL.prepare((int)(sr * 4.5)); delR.prepare((int)(sr * 4.5));
        reverb.prepare((float)sr);
        repL.assign((size_t)(sr * 6), 0.0f); repR.assign((size_t)(sr * 6), 0.0f); repW = 0;
        filterS.setTime(0.03f, (float)sr); filterS.snap(master[GP_M_FILTER].load());
        delayTime.setTime(0.25f, (float)sr); delayTime.snap(delaySamples());
        repMix.setTime(0.004f, (float)sr); repMix.snap(0);
        volumeS.setTime(0.02f, (float)sr); volumeS.snap(master[GP_M_VOLUME].load());
        mFiltL.reset(); mFiltR.reset();
        for (auto& v : voices) v.active = false;
        prepared = true;
    }

    float stepSamples() const {
        float bpm = clampf(master[GP_M_BPM].load(std::memory_order_relaxed), 40.0f, 240.0f);
        return (float)(sr * 60.0 / bpm / 4.0);
    }
    float delaySamples() const {
        int idx = (int)(clampf(master[GP_M_DELAY_TIME].load(), 0, 7) + 0.5f);
        return std::min(stepSamples() * kDelaySteps[idx], (float)(sr * 4.4));
    }
    float swingOffset(int step) const {
        float sw = clampf(master[GP_M_SWING].load(std::memory_order_relaxed), 0.0f, 0.5f);
        return (step & 1) ? sw * stepSamples() : 0.0f;
    }

    // -----------------------------------------------------------------------
    Voice* allocVoice() {
        Voice* best = nullptr;
        for (auto& v : voices) if (!v.active) return &v;
        // steal: oldest voice, prefer released ones
        for (auto& v : voices) {
            if (!best) { best = &v; continue; }
            bool vr = v.stage == 2, br = best->stage == 2;
            if (vr != br) { if (vr) best = &v; continue; }
            if (v.order < best->order) best = &v;
        }
        return best;
    }

    void kill(Voice& v) { if (v.active && !v.killing) { v.killing = true; v.killGain = 1; } }

    void startVoice(int pad, float vel, int autoOffSamples) {
        if (pad < 0 || pad >= GP_NUM_PADS) return;
        padHits[pad].fetch_add(1, std::memory_order_relaxed);
        const Sample* s = padSample[pad];
        if (!s || s->frames < 32) return;
        const PadSnap& P = snap[pad];
        int choke = (int)P.p[GP_P_CHOKE];
        bool mono = P.p[GP_P_MONO] > 0.5f;
        for (auto& v : voices) {
            if (!v.active || v.killing) continue;
            if ((mono && v.pad == pad) ||
                (choke > 0 && v.pad != pad && (int)snap[v.pad].p[GP_P_CHOKE] == choke))
                kill(v);
        }
        Voice* vp = allocVoice();
        if (vp->active) { // stolen: hard but short fade is handled by restarting declick
            vp->active = false;
        }
        Voice& v = *vp;
        v = Voice();
        v.active = true; v.pad = pad; v.s = s; v.order = ++voiceOrder; v.vel = clampf(vel, 0.0f, 1.0f);
        v.rng = Rng((uint32_t)(v.order * 2654435761u) ^ 0xA5A5u);
        v.stage = 0; v.env = 0; v.gate = true; v.autoOff = autoOffSamples;
        double s0, s1; region(*s, P, s0, s1);
        bool rev = P.p[GP_P_REVERSE] > 0.5f;
        v.pos = rev ? s1 - 1.0 : s0;
        v.scan = rev ? s1 - 1.0 : s0;
        v.declick = (s0 > 0.5 || rev) ? 0.0f : 1.0f;
        v.fL.reset(); v.fR.reset();
    }

    void releasePad(int pad) {
        for (auto& v : voices) if (v.active && v.pad == pad) v.gate = false;
    }

    static void region(const Sample& s, const PadSnap& P, double& s0, double& s1) {
        float a = clampf(P.p[GP_P_START], 0.0f, 1.0f), b = clampf(P.p[GP_P_END], 0.0f, 1.0f);
        if (b < a) std::swap(a, b);
        s0 = a * (s.frames - 1); s1 = b * (s.frames - 1);
        if (s1 - s0 < 32) s1 = std::min<double>(s.frames - 1, s0 + 32);
    }

    // -----------------------------------------------------------------------
    void handleCommands() {
        Cmd c;
        while (cmds.pop(c)) {
            switch (c.type) {
            case C_TRIGGER: liveTrigger(c.pad, c.value); break;
            case C_RELEASE: releasePad(c.pad); break;
            case C_ALL_OFF: for (auto& v : voices) kill(v); break;
            case C_PLAY:
                if (!playing) { playing = true; curStep = -1; toNext = 0; for (int& s : skipStep) s = -1; }
                break;
            case C_STOP:
                playing = false; recording = false; curStep = -1;
                if (queued >= 0) { curPattern = queued; queued = -1; }
                for (auto& v : voices) v.gate = false;
                break;
            case C_RECORD: recording = c.value > 0.5f; break;
            case C_PATTERN: {
                int p = (int)c.value;
                if (p < 0 || p >= GP_NUM_PATTERNS) break;
                if (playing && p != curPattern) queued = p;
                else { curPattern = p; queued = -1; }
                break;
            }
            case C_SAMPLE:
                for (auto& v : voices) if (v.active && v.pad == c.pad) v.active = false;
                padSample[c.pad] = c.sample;
                acks.push(c.seq);
                break;
            }
        }
    }

    void liveTrigger(int pad, float vel) {
        snapPad(pad);
        startVoice(pad, vel, -1);
        if (playing && recording && curStep >= 0) {
            int len = patternLen[curPattern].load();
            float stepLen = stepSamples();
            float elapsed = (float)(interval - toNext);
            int st = curStep;
            if (elapsed > stepLen * 0.5f) { st = (curStep + 1) % len; skipStep[pad] = st; }
            steps[curPattern][pad][st].store((uint8_t)std::max(1.0f, vel * 127.0f));
        }
    }

    void snapPad(int p) {
        for (int i = 0; i < GP_PAD_PARAM_COUNT; ++i) snap[p].p[i] = padParams[p][i].load(std::memory_order_relaxed);
    }

    void fireStep() {
        int len = patternLen[curPattern].load();
        int next = curStep + 1;
        if (next >= len || curStep < 0) {
            next = 0;
            if (queued >= 0) { curPattern = queued; queued = -1; len = patternLen[curPattern].load(); }
        }
        curStep = next;
        int metro = (int)master[GP_M_METRONOME].load();
        if ((curStep % 4) == 0 && (metro == 2 || (metro == 1 && recording))) {
            clickLeft = (int)(sr * 0.03); clickPhase = 0; clickFreq = curStep == 0 ? 1600.0f : 1000.0f;
        }
        float stepLen = stepSamples();
        for (int p = 0; p < GP_NUM_PADS; ++p) {
            uint8_t v = steps[curPattern][p][curStep].load(std::memory_order_relaxed);
            if (skipStep[p] == curStep) { skipStep[p] = -1; continue; }
            if (!v) continue;
            if (snap[p].p[GP_P_MUTE] > 0.5f) continue;
            startVoice(p, v / 127.0f, (int)(stepLen * 0.92f));
        }
        int nstep = curStep + 1 >= len ? 0 : curStep + 1;
        interval = stepLen + swingOffset(nstep) - swingOffset(curStep);
        if (nstep == 0) interval = stepLen - swingOffset(curStep);
        toNext += interval;
    }

    // -----------------------------------------------------------------------
    void renderVoices(int off, int n) {
        const float fsr = (float)sr;
        for (auto& v : voices) {
            if (!v.active) continue;
            const PadSnap& P = snap[v.pad];
            const Sample& S = *v.s;
            const float* dL = S.L.data();
            const float* dR = S.right();
            const int N = S.frames;
            double s0, s1; region(S, P, s0, s1);
            const double regionLen = s1 - s0;
            bool rev = P.p[GP_P_REVERSE] > 0.5f;
            bool loop = P.p[GP_P_LOOP] > 0.5f;
            bool oneShot = P.p[GP_P_ONESHOT] > 0.5f && !loop;
            int sync = (int)P.p[GP_P_SYNC];
            float srcBpm = P.p[GP_P_SRC_BPM];
            float tempoRatio = (sync > 0 && srcBpm > 20.0f)
                ? clampf(master[GP_M_BPM].load(std::memory_order_relaxed) / srcBpm, 0.25f, 4.0f) : 1.0f;
            bool texture = P.p[GP_P_MODE] > 0.5f || sync == 2;
            double pitchRatio = std::pow(2.0, (P.p[GP_P_PITCH] + P.p[GP_P_FINE] * 0.01) / 12.0);
            double baseInc = S.sampleRate / sr * pitchRatio * (sync == 1 ? tempoRatio : 1.0);

            // envelope coefficients
            float atk = std::max(P.p[GP_P_ATTACK], 0.0004f);
            float atkInc = 1.0f / (atk * fsr);
            float sus = clampf(P.p[GP_P_SUSTAIN], 0.0f, 1.0f);
            float decCoef = P.p[GP_P_DECAY] < 0.001f ? 0.0f : std::exp(-6.9f / (P.p[GP_P_DECAY] * fsr));
            float relCoef = std::exp(-6.9f / (std::max(P.p[GP_P_RELEASE], 0.003f) * fsr));

            // gain / pan
            float g = P.p[GP_P_GAIN];
            float gain = g <= 0.0001f ? 0.0f : std::pow(g / 0.8f, 2.0f);   // 0.8 = unity, 1.0 = +3.9 dB
            gain *= v.vel * (P.p[GP_P_MUTE] > 0.5f ? 0.0f : 1.0f);
            float pan = clampf(P.p[GP_P_PAN], -1.0f, 1.0f);
            float pl = std::cos((pan + 1.0f) * kPi * 0.25f) * 1.41421f;
            float pr = std::sin((pan + 1.0f) * kPi * 0.25f) * 1.41421f;
            pl = std::min(pl, 1.0f); pr = std::min(pr, 1.0f);

            bool filt = P.p[GP_P_CUTOFF] < 0.995f;
            if (filt) {
                float hz = 20.0f * std::pow(1000.0f, clampf(P.p[GP_P_CUTOFF], 0.0f, 1.0f));
                float q = 0.6f + P.p[GP_P_RESO] * 9.0f;
                v.fL.set(hz, q, fsr); v.fR.set(hz, q, fsr);
            }
            float ds = P.p[GP_P_DELAY_SEND], rs = P.p[GP_P_REVERB_SEND];

            // texture parameters
            float gSize = clampf(P.p[GP_P_GRAIN_SIZE], 0.01f, 0.5f);
            float gDens = clampf(P.p[GP_P_GRAIN_DENSITY], 1.0f, 120.0f);
            float spray = clampf(P.p[GP_P_GRAIN_SPRAY], 0.0f, 1.0f);
            float scanSpeed = clampf(P.p[GP_P_GRAIN_SCAN], 0.0f, 2.0f);
            float pRnd = clampf(P.p[GP_P_GRAIN_PITCHRND], 0.0f, 12.0f);
            float stereo = clampf(P.p[GP_P_GRAIN_STEREO], 0.0f, 1.0f);
            if (sync == 2) {   // elastic: keeps pitch, follows tempo
                gSize = 0.085f; gDens = 4.0f / gSize; spray = 0.0f; pRnd = 0.0f; stereo = 0.1f;
                scanSpeed = tempoRatio;
            }
            int gLen = std::max(16, (int)(gSize * fsr));
            float gInterval = fsr / gDens;
            float overlap = gDens * gSize;
            float gNorm = 1.0f / std::sqrt(std::max(1.0f, overlap)) * (sync == 2 ? 1.25f : 1.0f);
            double scanInc = (S.sampleRate / sr) * scanSpeed * (rev ? -1.0 : 1.0);

            float maxEnv = 0;
            for (int i = 0; i < n; ++i) {
                // auto note-off from sequencer
                if (v.autoOff > 0 && --v.autoOff == 0 && !oneShot) v.gate = false;
                // envelope
                bool released = !v.gate && !oneShot;
                if (released && v.stage < 2) v.stage = 2;
                if (v.stage == 0) { v.env += atkInc; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
                else if (v.stage == 1) { v.env = sus + (v.env - sus) * decCoef; }
                else { v.env *= relCoef; }
                if ((v.stage == 2 || (v.stage == 1 && sus <= 0.0001f)) && v.env < 0.0002f) { v.active = false; break; }
                float amp = v.env * gain;
                if (v.killing) { v.killGain -= 1.0f / (0.004f * fsr); if (v.killGain <= 0) { v.active = false; break; } amp *= v.killGain; }
                if (v.declick < 1.0f) { v.declick = std::min(1.0f, v.declick + 1.0f / (0.0015f * fsr)); amp *= v.declick; }

                float xl = 0, xr = 0;
                if (!texture) {
                    xl = readCubic(dL, N, v.pos);
                    xr = (S.channels > 1) ? readCubic(dR, N, v.pos) : xl;
                    double remaining;
                    if (!rev) {
                        v.pos += baseInc;
                        if (v.pos >= s1) {
                            if (loop && v.gate) v.pos -= regionLen; else { v.active = false; }
                        }
                        remaining = (s1 - v.pos) / baseInc;
                    } else {
                        v.pos -= baseInc;
                        if (v.pos <= s0) {
                            if (loop && v.gate) v.pos += regionLen; else { v.active = false; }
                        }
                        remaining = (v.pos - s0) / baseInc;
                    }
                    if (!(loop && v.gate) && remaining < 96.0) amp *= (float)std::max(0.0, remaining / 96.0);
                } else {
                    // spawn grains
                    v.nextGrain -= 1.0f;
                    if (v.nextGrain <= 0.0f) {
                        v.nextGrain += gInterval * (1.0f + (sync == 2 ? 0.05f : 0.3f) * v.rng.bi());
                        for (auto& gr : v.grains) {
                            if (gr.on) continue;
                            double p = v.scan + spray * regionLen * 0.5 * v.rng.bi();
                            if (p < s0) p += regionLen;
                            if (p > s1) p -= regionLen;
                            gr.on = true; gr.age = 0; gr.len = (int)(gLen * (sync == 2 ? 1.0f : (0.75f + 0.5f * v.rng.uni())));
                            float semis = pRnd * v.rng.bi();
                            if (pRnd > 6.0f && v.rng.uni() < 0.25f) semis = (v.rng.uni() < 0.5f ? -12.0f : 12.0f);
                            double ginc = S.sampleRate / sr * pitchRatio * std::pow(2.0, semis / 12.0);
                            gr.inc = rev ? -ginc : ginc;
                            gr.pos = p;
                            float gp = stereo * v.rng.bi();
                            gr.gl = std::min(1.0f, 1.0f - gp); gr.gr = std::min(1.0f, 1.0f + gp);
                            break;
                        }
                    }
                    for (auto& gr : v.grains) {
                        if (!gr.on) continue;
                        float w = hann[(int)((float)gr.age / gr.len * kHann)];
                        double rp = gr.pos;
                        while (rp >= N - 1) rp -= regionLen;
                        while (rp < 0) rp += regionLen;
                        float a = readCubic(dL, N, rp);
                        float b = (S.channels > 1) ? readCubic(dR, N, rp) : a;
                        xl += a * w * gr.gl; xr += b * w * gr.gr;
                        gr.pos += gr.inc;
                        if (++gr.age >= gr.len) gr.on = false;
                    }
                    xl *= gNorm; xr *= gNorm;
                    v.scan += scanInc;
                    v.scanned += std::fabs(scanInc);
                    if (v.scan >= s1) v.scan -= regionLen;
                    if (v.scan < s0) v.scan += regionLen;
                    // one-shot texture: one pass through the region (or 2 s when frozen)
                    if (oneShot && v.stage < 2) {
                        bool done = scanSpeed > 0.05f ? v.scanned >= regionLen : (++v.age > (int)(2.0 * sr));
                        if (done) { v.stage = 2; }
                    }
                }
                if (filt) { xl = v.fL.lp(xl); xr = v.fR.lp(xr); }
                float ol = xl * amp * pl, orr = xr * amp * pr;
                int k = off + i;
                dryL[k] += ol; dryR[k] += orr;
                if (ds > 0) { dlyL[k] += ol * ds; dlyR[k] += orr * ds; }
                if (rs > 0) { revL[k] += ol * rs; revR[k] += orr * rs; }
                maxEnv = std::max(maxEnv, v.env * v.vel);
                if (!v.active) break;
            }
            float act = padActivity[v.pad].load(std::memory_order_relaxed);
            if (maxEnv > act) padActivity[v.pad].store(maxEnv, std::memory_order_relaxed);
            if (v.active && v.order == latestOrder(v.pad)) {
                double pp = texture ? v.scan : v.pos;
                padPlayhead[v.pad].store((float)(pp / std::max(1, N - 1)), std::memory_order_relaxed);
            }
        }
    }

    uint64_t latestOrder(int pad) const {
        uint64_t o = 0;
        for (auto& v : voices) if (v.active && v.pad == pad && v.order > o) o = v.order;
        return o;
    }

    // -----------------------------------------------------------------------
    void render(float* outL, float* outR, int frames) {
        if (!prepared) { std::memset(outL, 0, sizeof(float) * frames); std::memset(outR, 0, sizeof(float) * frames); return; }
        int done = 0;
        while (done < frames) {
            int chunk = std::min(frames - done, maxFrames);
            renderChunk(outL + done, outR + done, chunk);
            done += chunk;
        }
    }

    void renderChunk(float* outL, float* outR, int frames) {
        for (int p = 0; p < GP_NUM_PADS; ++p) snapPad(p);
        handleCommands();
        for (int p = 0; p < GP_NUM_PADS; ++p) {
            float a = padActivity[p].load(std::memory_order_relaxed);
            padActivity[p].store(a * 0.9f, std::memory_order_relaxed);
        }
        for (auto* b : {&dryL, &dryR, &dlyL, &dlyR, &revL, &revR}) std::fill(b->begin(), b->begin() + frames, 0.0f);

        // sequencer: split the block at step boundaries
        int done = 0;
        while (done < frames) {
            int n = frames - done;
            if (playing) {
                if (toNext <= 0.0) { fireStep(); continue; }
                n = std::min(n, std::max(1, (int)std::ceil(toNext)));
            }
            renderVoices(done, n);
            if (playing) toNext -= n;
            done += n;
        }
        for (int p = 0; p < GP_NUM_PADS; ++p) {
            bool any = false;
            for (auto& v : voices) if (v.active && v.pad == p) { any = true; break; }
            if (!any) padPlayhead[p].store(-1.0f, std::memory_order_relaxed);
        }

        masterFx(outL, outR, frames);

        aPlaying.store(playing); aRecording.store(recording);
        aStep.store(playing ? curStep : -1); aPattern.store(curPattern); aQueued.store(queued);
    }

    void masterFx(float* outL, float* outR, int frames) {
        const float fsr = (float)sr;
        // parameters
        filterS.target = master[GP_M_FILTER].load();
        float reso = master[GP_M_RESO].load();
        float drive = master[GP_M_DRIVE].load();
        float crush = master[GP_M_CRUSH].load();
        float fb = clampf(master[GP_M_DELAY_FB].load(), 0.0f, 0.95f);
        float tone = master[GP_M_DELAY_TONE].load();
        toneL.setHz(800.0f * std::pow(18.0f, tone), fsr); toneR.a = toneL.a;
        delayTime.target = delaySamples();
        reverb.setParams(master[GP_M_REVERB_SIZE].load(), master[GP_M_REVERB_DAMP].load());
        volumeS.target = master[GP_M_VOLUME].load();
        int repIdx = (int)(clampf(master[GP_M_REPEAT].load(), 0, 4) + 0.5f);
        float driveGain = 1.0f + drive * drive * 40.0f;
        float driveNorm = 0.3f / std::tanh(driveGain * 0.3f);
        int bits = 16 - (int)(crush * 13.0f);
        float levels = (float)(1 << std::max(bits, 2));
        float hold = 1.0f + crush * crush * 40.0f;

        if (repIdx > 0 && !repActive) {
            repLen = std::max(64, (int)(stepSamples() * kRepeatSteps[repIdx]));
            repLen = std::min<int>(repLen, (int)repL.size() - 1);
            repStart = repW - repLen; if (repStart < 0) repStart += (int)repL.size();
            repR_ = 0; repActive = true;
        }
        if (repIdx > 0 && repActive) {
            int want = std::max(64, std::min<int>((int)(stepSamples() * kRepeatSteps[repIdx]), (int)repL.size() - 1));
            if (want < repLen) { repLen = want; if (repR_ >= repLen) repR_ = 0; }
        }
        repMix.target = repIdx > 0 ? 1.0f : 0.0f;

        float pk[2] = {0, 0};
        const int RN = (int)repL.size();
        for (int i = 0; i < frames; ++i) {
            // delay (ping-pong)
            float dt = delayTime.next();
            float dl = delL.read(dt), dr = delR.read(dt);
            float din = (dlyL[i] + dlyR[i]) * 0.5f;
            delL.push(din + toneR.lp(dr) * fb);
            delR.push(toneL.lp(dl) * fb);
            // reverb
            float rl, rr; reverb.tick(revL[i], revR[i], rl, rr);
            float l = dryL[i] + dl * 0.8f + rl * 0.55f;
            float r = dryR[i] + dr * 0.8f + rr * 0.55f;

            // DJ filter
            if ((filterCount++ & 15) == 0) {
                float f = filterS.cur;
                float dist = std::fabs(f - 0.5f);
                filterOn = dist > 0.015f;
                if (filterOn) {
                    float q = 0.707f + reso * 7.0f * std::min(1.0f, dist / 0.12f);
                    if (f < 0.5f) { filterHP = false; mFiltL.set(20000.0f * std::pow(2.0f, -(0.5f - f) / 0.5f * 8.5f), q, fsr); }
                    else { filterHP = true; mFiltL.set(20.0f * std::pow(2.0f, (f - 0.5f) / 0.5f * 8.5f), q, fsr); }
                    mFiltR.g = mFiltL.g; mFiltR.k = mFiltL.k; mFiltR.a1 = mFiltL.a1; mFiltR.a2 = mFiltL.a2; mFiltR.a3 = mFiltL.a3;
                }
            }
            filterS.next();
            if (filterOn) {
                if (filterHP) { l = mFiltL.hp(l); r = mFiltR.hp(r); }
                else { l = mFiltL.lp(l); r = mFiltR.lp(r); }
            } else { mFiltL.lp(l); mFiltR.lp(r); }   // keep state warm to avoid clicks

            if (drive > 0.005f) {
                float wl = std::tanh(l * driveGain) * driveNorm, wr = std::tanh(r * driveGain) * driveNorm;
                float m = std::min(1.0f, drive * 4.0f);
                l += (wl - l) * m; r += (wr - r) * m;
            }
            if (crush > 0.005f) {
                crushCount += 1.0f;
                if (crushCount >= hold) {
                    crushCount -= hold;
                    crushHold[0] = std::round(l * levels) / levels;
                    crushHold[1] = std::round(r * levels) / levels;
                }
                float m = std::min(1.0f, crush * 5.0f);
                l += (crushHold[0] - l) * m; r += (crushHold[1] - r) * m;
            }

            // metronome
            if (clickLeft > 0) {
                float e = (float)clickLeft / (float)(sr * 0.03);
                float c = std::sin(clickPhase) * e * e * 0.25f;
                clickPhase += 2.0f * kPi * clickFreq / fsr;
                --clickLeft;
                l += c; r += c;
            }

            // stutter / repeat
            float mix = repMix.next();
            if (!repActive || mix < 1.0f) {
                if (!repActive) { repL[repW] = l; repR[repW] = r; if (++repW >= RN) repW = 0; }
            }
            if (repActive) {
                int idx = repStart + repR_; if (idx >= RN) idx -= RN;
                float edge = std::min(1.0f, std::min((float)repR_, (float)(repLen - repR_)) / 48.0f);
                float ll = repL[idx] * edge, rr2 = repR[idx] * edge;
                if (++repR_ >= repLen) repR_ = 0;
                l = l + (ll - l) * mix; r = r + (rr2 - r) * mix;
                if (repIdx == 0 && mix < 0.001f) repActive = false;
            }

            float vol = volumeS.next();
            float vg = vol * vol * 1.5625f;   // 0.8 -> unity
            l *= vg; r *= vg;

            // limiter
            float peak = std::max(std::fabs(l), std::fabs(r));
            const float thr = 0.94f;
            float target = peak > thr ? thr / peak : 1.0f;
            if (target < limGain) limGain = target; else limGain += (1.0f - limGain) * 0.0004f;
            l *= limGain; r *= limGain;
            l = clampf(l, -1.0f, 1.0f); r = clampf(r, -1.0f, 1.0f);
            outL[i] = l; outR[i] = r;
            pk[0] = std::max(pk[0], std::fabs(l)); pk[1] = std::max(pk[1], std::fabs(r));
        }
        for (int c = 0; c < 2; ++c) {
            float o = outPeak[c].load(std::memory_order_relaxed) * 0.85f;
            outPeak[c].store(std::max(o, pk[c]), std::memory_order_relaxed);
        }
    }

    // -----------------------------------------------------------------------
    // main thread
    void sendSample(int pad, std::shared_ptr<Sample> s) {
        Cmd c; c.type = C_SAMPLE; c.pad = pad; c.sample = s.get(); c.seq = nextSeq++;
        pending.emplace_back(c.seq, registry[pad]);
        registry[pad] = std::move(s);
        if (!cmds.push(c)) {
            // queue full: the audio thread is not running; apply directly
            padSample[pad] = c.sample;
            acks.push(c.seq);
        }
        collect();
    }

    void collect() {
        uint32_t a, last = 0;
        bool any = false;
        while (acks.pop(a)) { last = a; any = true; }
        if (any) {
            while (!pending.empty() && pending.front().first <= last) pending.pop_front();
        }
    }

    void push(CmdType t, int pad, float value) {
        Cmd c; c.type = t; c.pad = pad; c.value = value;
        cmds.push(c);
    }
};

static Engine& E() { static Engine* e = new Engine(); return *e; }

std::shared_ptr<Sample> padSample(int pad) {
    if (pad < 0 || pad >= GP_NUM_PADS) return nullptr;
    return E().registry[pad];
}
void assignPadSample(int pad, std::shared_ptr<Sample> s) {
    if (pad < 0 || pad >= GP_NUM_PADS) return;
    E().sendSample(pad, std::move(s));
}
double engineBpm() { return E().master[GP_M_BPM].load(); }

} // namespace gp

using namespace gp;

// ============================================================================
// C API
static bool padOk(int p) { return p >= 0 && p < GP_NUM_PADS; }
static bool patOk(int p) { return p >= 0 && p < GP_NUM_PATTERNS; }

void gp_prepare(double sampleRate, int maxFrames) { E().prepare(sampleRate, maxFrames); }
void gp_render(float* l, float* r, int frames) { E().render(l, r, frames); }
double gp_sample_rate(void) { return E().sr; }

void gp_set_pad_param(int pad, int param, float value) {
    if (padOk(pad) && param >= 0 && param < GP_PAD_PARAM_COUNT) E().padParams[pad][param].store(value);
}
float gp_get_pad_param(int pad, int param) {
    if (padOk(pad) && param >= 0 && param < GP_PAD_PARAM_COUNT) return E().padParams[pad][param].load();
    return 0;
}
void gp_reset_pad_params(int pad) {
    if (!padOk(pad)) return;
    for (int i = 0; i < GP_PAD_PARAM_COUNT; ++i) E().padParams[pad][i].store(kPadDefaults[i]);
}
void gp_set_master(int p, float v) { if (p >= 0 && p < GP_M_COUNT) E().master[p].store(v); }
float gp_get_master(int p) { return (p >= 0 && p < GP_M_COUNT) ? E().master[p].load() : 0.0f; }

void gp_trigger(int pad, float vel) { if (padOk(pad)) E().push(C_TRIGGER, pad, vel); }
void gp_release(int pad) { if (padOk(pad)) E().push(C_RELEASE, pad, 0); }
void gp_all_notes_off(void) { E().push(C_ALL_OFF, 0, 0); }

void gp_set_playing(int on) { E().push(on ? C_PLAY : C_STOP, 0, 0); }
int gp_is_playing(void) { return E().aPlaying.load(); }
void gp_set_recording(int on) { E().push(C_RECORD, 0, on ? 1.0f : 0.0f); }
int gp_is_recording(void) { return E().aRecording.load(); }
void gp_select_pattern(int p) { if (patOk(p)) E().push(C_PATTERN, 0, (float)p); }
int gp_current_pattern(void) { return E().aPattern.load(); }
int gp_queued_pattern(void) { return E().aQueued.load(); }
int gp_current_step(void) { return E().aStep.load(); }
void gp_set_pattern_length(int p, int steps) {
    if (!patOk(p)) return;
    E().patternLen[p].store(std::max(1, std::min(GP_MAX_STEPS, steps)));
}
int gp_get_pattern_length(int p) { return patOk(p) ? E().patternLen[p].load() : 16; }
void gp_set_step(int p, int pad, int s, float vel) {
    if (!patOk(p) || !padOk(pad) || s < 0 || s >= GP_MAX_STEPS) return;
    E().steps[p][pad][s].store(vel <= 0.0f ? 0 : (uint8_t)std::max(1.0f, std::min(127.0f, vel * 127.0f)));
}
float gp_get_step(int p, int pad, int s) {
    if (!patOk(p) || !padOk(pad) || s < 0 || s >= GP_MAX_STEPS) return 0;
    return E().steps[p][pad][s].load() / 127.0f;
}
void gp_clear_pattern(int p) { if (patOk(p)) for (int pad = 0; pad < GP_NUM_PADS; ++pad) gp_clear_pad_steps(p, pad); }
void gp_clear_pad_steps(int p, int pad) {
    if (!patOk(p) || !padOk(pad)) return;
    for (int s = 0; s < GP_MAX_STEPS; ++s) E().steps[p][pad][s].store(0);
}
void gp_copy_pattern(int from, int to) {
    if (!patOk(from) || !patOk(to) || from == to) return;
    for (int pad = 0; pad < GP_NUM_PADS; ++pad)
        for (int s = 0; s < GP_MAX_STEPS; ++s) E().steps[to][pad][s].store(E().steps[from][pad][s].load());
    E().patternLen[to].store(E().patternLen[from].load());
}
void gp_apply_steps(int p, const float* st) {
    if (!patOk(p) || !st) return;
    for (int pad = 0; pad < GP_NUM_PADS; ++pad)
        for (int s = 0; s < GP_MAX_STEPS; ++s) gp_set_step(p, pad, s, st[pad * GP_MAX_STEPS + s]);
}
void gp_read_steps(int p, float* out) {
    if (!patOk(p) || !out) return;
    for (int pad = 0; pad < GP_NUM_PADS; ++pad)
        for (int s = 0; s < GP_MAX_STEPS; ++s) out[pad * GP_MAX_STEPS + s] = gp_get_step(p, pad, s);
}

// ---- samples
static void trimAndNormalize(std::vector<float>& L, std::vector<float>& R, int& frames, double sr, int autoTrim, int normalize) {
    bool stereo = !R.empty();
    auto mag = [&](int i) { return stereo ? std::max(std::fabs(L[i]), std::fabs(R[i])) : std::fabs(L[i]); };
    float peak = 0;
    for (int i = 0; i < frames; ++i) peak = std::max(peak, mag(i));
    if (peak < 1e-6f) return;
    if (autoTrim) {
        float thr = std::max(peak * 0.02f, 0.0015f);   // -34 dB relative or -56 dBFS
        int a = 0, b = frames - 1;
        while (a < frames && mag(a) < thr) ++a;
        while (b > a && mag(b) < thr) --b;
        a = std::max(0, a - (int)(sr * 0.004));
        b = std::min(frames - 1, b + (int)(sr * 0.05));
        if (b - a > 64) {
            int n = b - a + 1;
            std::vector<float> nl(L.begin() + a, L.begin() + a + n);
            L.swap(nl);
            if (stereo) { std::vector<float> nr(R.begin() + a, R.begin() + a + n); R.swap(nr); }
            frames = n;
            // tiny fades so trimmed edges never click
            int fi = std::min(frames / 4, (int)(sr * 0.002)), fo = std::min(frames / 4, (int)(sr * 0.01));
            for (int i = 0; i < fi; ++i) { float g = (float)i / fi; L[i] *= g; if (stereo) R[i] *= g; }
            for (int i = 0; i < fo; ++i) { float g = (float)i / fo; L[frames - 1 - i] *= g; if (stereo) R[frames - 1 - i] *= g; }
        }
    }
    if (normalize) {
        float g = 0.891f / peak;   // -1 dBFS
        g = std::min(g, 32.0f);
        for (int i = 0; i < frames; ++i) { L[i] *= g; if (stereo) R[i] *= g; }
    }
}

int gp_load_pad_sample(int pad, const float* left, const float* right, int frames, double sampleRate, int autoTrim, int normalize) {
    if (!padOk(pad) || !left || frames < 32) return 0;
    auto s = std::make_shared<Sample>();
    s->L.assign(left, left + frames);
    bool stereo = right && right != left;
    if (stereo) {
        // collapse to mono when the channels are identical
        bool same = true;
        for (int i = 0; i < frames && same; i += 7) if (std::fabs(left[i] - right[i]) > 1e-5f) same = false;
        if (!same) s->R.assign(right, right + frames);
    }
    int n = frames;
    trimAndNormalize(s->L, s->R, n, sampleRate, autoTrim, normalize);
    s->frames = n;
    s->channels = s->R.empty() ? 1 : 2;
    s->sampleRate = sampleRate > 1000 ? sampleRate : 48000.0;
    s->id = E().nextSampleId++;
    E().padParams[pad][GP_P_START].store(0.0f);
    E().padParams[pad][GP_P_END].store(1.0f);
    E().sendSample(pad, s);
    return n;
}

void gp_share_pad_sample(int dst, int src) {
    if (!padOk(dst) || !padOk(src) || dst == src) return;
    E().sendSample(dst, E().registry[src]);
}
void gp_clear_pad_sample(int pad) { if (padOk(pad)) E().sendSample(pad, nullptr); }
int gp_pad_has_sample(int pad) { return padOk(pad) && E().registry[pad] ? 1 : 0; }
int gp_pad_frames(int pad) { return padOk(pad) && E().registry[pad] ? E().registry[pad]->frames : 0; }
int gp_pad_channels(int pad) { return padOk(pad) && E().registry[pad] ? E().registry[pad]->channels : 0; }
double gp_pad_sample_rate(int pad) { return padOk(pad) && E().registry[pad] ? E().registry[pad]->sampleRate : 0; }
long gp_pad_sample_id(int pad) { return padOk(pad) && E().registry[pad] ? E().registry[pad]->id : 0; }
int gp_pad_copy_audio(int pad, float* l, float* r, int maxFrames) {
    if (!padOk(pad) || !E().registry[pad]) return 0;
    const Sample& s = *E().registry[pad];
    int n = std::min(maxFrames, s.frames);
    if (l) std::memcpy(l, s.L.data(), sizeof(float) * n);
    if (r) std::memcpy(r, s.right(), sizeof(float) * n);
    return n;
}
void gp_pad_peaks(int pad, float* out, int bins) {
    if (!out || bins <= 0) return;
    std::fill(out, out + bins * 2, 0.0f);
    if (!padOk(pad) || !E().registry[pad]) return;
    const Sample& s = *E().registry[pad];
    const float* R = s.right();
    for (int b = 0; b < bins; ++b) {
        int a = (int)((long long)b * s.frames / bins);
        int e = std::max(a + 1, (int)((long long)(b + 1) * s.frames / bins));
        float mn = 0, mx = 0;
        int stride = std::max(1, (e - a) / 256);
        for (int i = a; i < e && i < s.frames; i += stride) {
            float v = (s.L[i] + R[i]) * 0.5f;
            mn = std::min(mn, v); mx = std::max(mx, v);
        }
        out[b * 2] = mn; out[b * 2 + 1] = mx;
    }
}
void gp_collect_garbage(void) { E().collect(); }

float gp_pad_activity(int pad) { return padOk(pad) ? E().padActivity[pad].load() : 0; }
float gp_pad_playhead(int pad) { return padOk(pad) ? E().padPlayhead[pad].load() : -1; }
float gp_output_peak(int ch) { return E().outPeak[ch ? 1 : 0].load(); }
unsigned gp_pad_hit_counter(int pad) { return padOk(pad) ? E().padHits[pad].load() : 0; }

// ---- offline bounce: renders a pattern on a private engine copy (main thread)
static double bounceStep(double sr) {
    float bpm = clampf(E().master[GP_M_BPM].load(), 40.0f, 240.0f);
    return sr * 15.0 / bpm;
}

int gp_bounce_frames(int pattern, int loops, float tailSeconds, double sr) {
    if (!patOk(pattern) || loops < 1 || sr < 8000) return 0;
    int len = E().patternLen[pattern].load();
    return (int)std::ceil(loops * len * bounceStep(sr) + std::max(0.0f, tailSeconds) * sr);
}

int gp_bounce(int pattern, int loops, float tailSeconds, double sr, float* L, float* R, int maxFrames) {
    if (!patOk(pattern) || !L || !R || loops < 1) return 0;
    Engine& src = E();
    int total = std::min(maxFrames, gp_bounce_frames(pattern, loops, tailSeconds, sr));
    if (total <= 0) return 0;
    auto* e = new Engine();
    for (int p = 0; p < GP_NUM_PADS; ++p) {
        for (int i = 0; i < GP_PAD_PARAM_COUNT; ++i) e->padParams[p][i].store(src.padParams[p][i].load());
        e->padSample[p] = src.registry[p].get();   // kept alive by the registry while we render
    }
    for (int i = 0; i < GP_M_COUNT; ++i) e->master[i].store(src.master[i].load());
    e->master[GP_M_REPEAT].store(0);
    e->master[GP_M_METRONOME].store(0);
    for (int a = 0; a < GP_NUM_PATTERNS; ++a) {
        e->patternLen[a].store(src.patternLen[a].load());
        for (int p = 0; p < GP_NUM_PADS; ++p)
            for (int s = 0; s < GP_MAX_STEPS; ++s) e->steps[a][p][s].store(src.steps[a][p][s].load());
    }
    e->prepare(sr, 1024);
    e->curPattern = pattern; e->queued = -1;
    e->playing = true; e->curStep = -1; e->toNext = 0;
    int music = std::min(total, (int)std::round(loops * src.patternLen[pattern].load() * bounceStep(sr)));
    e->render(L, R, music);
    e->playing = false;
    for (auto& v : e->voices) v.gate = false;
    if (total > music) e->render(L + music, R + music, total - music);
    delete e;
    return total;
}
