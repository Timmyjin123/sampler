// Offline smart tools: tempo detection, transient chop, pattern variations.
// Runs on the main thread; never touches the audio thread directly.
#include "GPEngine.h"
#include "GPInternal.hpp"
#include "GPDSP.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

using namespace gp;

namespace {

// Onset strength of a sample: half-wave rectified log-energy rise of a
// pre-emphasized signal, one value per hop.
std::vector<float> onsetCurve(const Sample& s, int hop) {
    const float* R = s.right();
    int hops = s.frames / hop;
    std::vector<float> energy(hops, 0.0f), onset(hops, 0.0f);
    float prev = 0;
    for (int h = 0; h < hops; ++h) {
        double e = 0;
        for (int i = h * hop; i < (h + 1) * hop; ++i) {
            float x = (s.L[i] + R[i]) * 0.5f;
            float y = x - 0.97f * prev;  // pre-emphasis favours attacks
            prev = x;
            e += y * y;
        }
        energy[h] = std::log(1e-9f + (float)(e / hop));
    }
    for (int h = 1; h < hops; ++h) onset[h] = std::max(0.0f, energy[h] - energy[h - 1]);
    return onset;
}

float tempoScore(const std::vector<float>& on, double lagHops) {
    // autocorrelation at a fractional lag plus its double (bar-level evidence)
    auto ac = [&](double lag) {
        double sum = 0; int n = 0;
        for (size_t i = 0; i + (size_t)lag + 1 < on.size(); ++i) {
            double j = i + lag; size_t k = (size_t)j; double f = j - k;
            sum += on[i] * (on[k] * (1 - f) + on[k + 1] * f); ++n;
        }
        return n ? sum / n : 0.0;
    };
    return (float)(ac(lagHops) + 0.5 * ac(lagHops * 2.0) + 0.25 * ac(lagHops * 4.0));
}

} // namespace

float gp_detect_bpm(int pad) {
    auto sp = padSample(pad);
    if (!sp) return 0.0f;
    const Sample& s = *sp;
    double dur = s.frames / s.sampleRate;
    if (dur < 1.0) return 0.0f;
    const int hop = std::max(64, (int)(s.sampleRate / 200.0));   // ~5 ms
    double hopsPerSec = s.sampleRate / hop;
    auto on = onsetCurve(s, hop);
    // need real attacks: at least a few clear energy jumps (> ~6 dB in 5 ms)
    int attacks = 0;
    for (size_t h = 1; h + 1 < on.size(); ++h)
        if (on[h] > 1.4f && on[h] >= on[h - 1] && on[h] >= on[h + 1]) ++attacks;
    if (attacks < 3) return 0.0f;
    double mean = std::accumulate(on.begin(), on.end(), 0.0) / std::max<size_t>(1, on.size());
    for (auto& v : on) v = std::max(0.0f, (float)(v - mean));

    float bestScore = 0, bestBpm = 0;
    std::vector<std::pair<float, float>> scores;
    for (float bpm = 60.0f; bpm <= 200.0f; bpm += 0.5f) {
        double lag = hopsPerSec * 60.0 / bpm;
        float sc = tempoScore(on, lag);
        // gentle preference for the 80..160 range
        float prior = std::exp(-0.5f * std::pow(std::log2(bpm / 115.0f) / 0.6f, 2.0f));
        sc *= 0.6f + 0.4f * prior;
        scores.push_back({bpm, sc});
        if (sc > bestScore) { bestScore = sc; bestBpm = bpm; }
    }
    if (bestScore <= 1e-9f) return 0.0f;

    // A loop usually spans a whole number of bars: if such a tempo scores
    // nearly as well as the best guess, prefer it — it will loop perfectly.
    float loopBpm = 0, loopScore = 0;
    for (int beats : {4, 8, 12, 16, 24, 32, 64}) {
        float b = (float)(beats * 60.0 / dur);
        if (b < 60.0f || b > 200.0f) continue;
        float sc = tempoScore(on, hopsPerSec * 60.0 / b);
        float prior = std::exp(-0.5f * std::pow(std::log2(b / 115.0f) / 0.6f, 2.0f));
        sc *= 0.6f + 0.4f * prior;
        if (sc > loopScore) { loopScore = sc; loopBpm = b; }
    }
    float result = bestBpm;
    if (loopBpm > 0 && loopScore > bestScore * 0.7f) result = loopBpm;
    // check confidence: peak vs average
    double avg = 0; for (auto& p : scores) avg += p.second; avg /= scores.size();
    if (bestScore < avg * 1.15) return 0.0f;
    return std::round(result * 10.0f) / 10.0f;
}

int gp_smart_chop(int pad, int firstPad, int maxSlices, int equalSlices, int writePattern) {
    auto sp = padSample(pad);
    if (!sp || sp->frames < 2048) return 0;
    const Sample& s = *sp;
    maxSlices = std::max(1, std::min(GP_NUM_PADS, maxSlices));
    firstPad = ((firstPad % GP_NUM_PADS) + GP_NUM_PADS) % GP_NUM_PADS;

    // honour the current trim of the source pad
    float t0 = gp_get_pad_param(pad, GP_P_START), t1 = gp_get_pad_param(pad, GP_P_END);
    if (t1 < t0) std::swap(t0, t1);
    int r0 = (int)(t0 * (s.frames - 1)), r1 = (int)(t1 * (s.frames - 1));
    if (r1 - r0 < 2048) { r0 = 0; r1 = s.frames - 1; }

    std::vector<int> cuts;
    if (equalSlices) {
        for (int i = 0; i < maxSlices; ++i) cuts.push_back(r0 + (int)((long long)(r1 - r0) * i / maxSlices));
    } else {
        const int hop = 256;
        auto on = onsetCurve(s, hop);
        int h0 = r0 / hop, h1 = std::min((int)on.size(), r1 / hop);
        int minGap = std::max(1, (int)(0.07 * s.sampleRate / hop));
        int win = std::max(4, (int)(0.25 * s.sampleRate / hop));
        std::vector<std::pair<float, int>> peaks;
        for (int h = h0 + 1; h < h1 - 1; ++h) {
            float v = on[h];
            if (v <= 0) continue;
            bool isMax = true;
            for (int k = std::max(h0, h - 3); k <= std::min(h1 - 1, h + 3); ++k) if (on[k] > v) { isMax = false; break; }
            if (!isMax) continue;
            double m = 0, m2 = 0; int n = 0;
            for (int k = std::max(h0, h - win); k < std::min(h1, h + win); ++k) { m += on[k]; m2 += on[k] * on[k]; ++n; }
            m /= n; double sd = std::sqrt(std::max(0.0, m2 / n - m * m));
            if (v > m + 1.2 * sd && v > 0.15f) peaks.push_back({v, h});
        }
        std::sort(peaks.begin(), peaks.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::vector<int> chosen;
        for (auto& p : peaks) {
            bool farEnough = true;
            for (int c : chosen) if (std::abs(c - p.second) < minGap) { farEnough = false; break; }
            if (std::abs(p.second - h0) < minGap) farEnough = false;
            if (farEnough) chosen.push_back(p.second);
            if ((int)chosen.size() >= maxSlices - 1) break;
        }
        cuts.push_back(r0);
        for (int h : chosen) cuts.push_back(std::max(r0, h * hop - (int)(0.003 * s.sampleRate)));
        std::sort(cuts.begin(), cuts.end());
        // not enough transients: fill the biggest gaps evenly
        while ((int)cuts.size() < maxSlices && equalSlices == 0 && cuts.size() < 2) {
            cuts.push_back(r0 + (r1 - r0) / 2);
            std::sort(cuts.begin(), cuts.end());
        }
    }

    float bpm = gp_detect_bpm(pad);
    int n = (int)cuts.size();
    for (int i = 0; i < n; ++i) {
        int dst = (firstPad + i) % GP_NUM_PADS;
        if (dst != pad) assignPadSample(dst, sp);
        gp_reset_pad_params(dst);
        float a = (float)cuts[i] / (s.frames - 1);
        float b = (float)((i + 1 < n) ? cuts[i + 1] : r1) / (s.frames - 1);
        gp_set_pad_param(dst, GP_P_START, a);
        gp_set_pad_param(dst, GP_P_END, b);
        gp_set_pad_param(dst, GP_P_ONESHOT, 1.0f);
        gp_set_pad_param(dst, GP_P_MONO, 1.0f);
        gp_set_pad_param(dst, GP_P_RELEASE, 0.02f);
        if (bpm > 0) {
            gp_set_pad_param(dst, GP_P_SRC_BPM, bpm);
            gp_set_pad_param(dst, GP_P_SYNC, 1.0f);
        }
    }

    if (writePattern >= 0 && writePattern < GP_NUM_PATTERNS && bpm > 0) {
        double stepFrames = s.sampleRate * 15.0 / bpm;
        int len = (int)std::round((r1 - r0) / stepFrames);
        len = std::max(1, std::min(GP_MAX_STEPS, len));
        // round up to a musical length
        for (int l : {4, 8, 16, 32, 48, 64}) if (len <= l) { len = l; break; }
        gp_clear_pattern(writePattern);
        gp_set_pattern_length(writePattern, len);
        for (int i = 0; i < n; ++i) {
            int st = (int)std::round((cuts[i] - r0) / stepFrames);
            if (st >= 0 && st < len) gp_set_step(writePattern, (firstPad + i) % GP_NUM_PADS, st, 1.0f);
        }
    }
    return n;
}

void gp_make_variation(int pattern, unsigned seed, float amount, float* out) {
    if (!out || pattern < 0 || pattern >= GP_NUM_PATTERNS) return;
    gp_read_steps(pattern, out);
    const int len = gp_get_pattern_length(pattern);
    amount = clampf(amount, 0.0f, 1.0f);
    Rng rng(seed * 2654435761u + 12345u);
    auto at = [&](int pad, int st) -> float& { return out[pad * GP_MAX_STEPS + st]; };

    // groups of pads that play slices of the same sample (chopped loops)
    std::map<long, std::vector<int>> groups;
    for (int p = 0; p < GP_NUM_PADS; ++p) if (long id = gp_pad_sample_id(p)) groups[id].push_back(p);

    // 1) reshuffle slices inside chopped groups — the classic flip
    for (auto& kv : groups) {
        auto& g = kv.second;
        if (g.size() < 4) continue;
        std::vector<std::pair<int, int>> hits;   // step, pad
        for (int p : g) for (int st = 0; st < len; ++st) if (at(p, st) > 0) hits.push_back({st, p});
        for (auto& h : hits) {
            if (rng.uni() > amount * 0.55f) continue;
            int np = g[rng.next() % g.size()];
            float v = at(h.second, h.first);
            at(h.second, h.first) = 0;
            at(np, h.first) = std::max(at(np, h.first), v);
        }
    }

    // 2) per-pad rhythmic edits
    for (int p = 0; p < GP_NUM_PADS; ++p) {
        int hits = 0, downbeats = 0;
        for (int st = 0; st < len; ++st) if (at(p, st) > 0) { ++hits; if (st % 4 == 0) ++downbeats; }
        if (hits == 0) continue;
        bool anchor = downbeats * 2 >= hits;          // kick-like: keep the downbeats
        float density = (float)hits / len;
        std::vector<float> row(len);
        for (int st = 0; st < len; ++st) row[st] = at(p, st);
        for (int st = 0; st < len; ++st) {
            float v = row[st];
            if (v > 0) {
                bool protectedHit = anchor && st % 4 == 0;
                float r = rng.uni();
                if (!protectedHit && r < amount * 0.2f) { at(p, st) = 0; continue; }                 // drop
                if (!protectedHit && r < amount * 0.35f) {                                            // nudge
                    int ns = (st + (rng.uni() < 0.5f ? -1 : 1) + len) % len;
                    if (row[ns] == 0) { at(p, st) = 0; at(p, ns) = v; continue; }
                }
                if (density > 0.2f && rng.uni() < amount * 0.18f) {                                    // double
                    int ns = (st + 1) % len;
                    if (at(p, ns) == 0) at(p, ns) = v * 0.6f;
                }
                at(p, st) = clampf(v * (1.0f + 0.25f * amount * rng.bi()), 0.2f, 1.0f);               // accents
            } else {
                float ghost = amount * (0.04f + 0.25f * density) * (st % 2 ? 1.0f : 0.5f);
                if (!anchor && rng.uni() < ghost) at(p, st) = 0.35f + 0.25f * rng.uni();               // ghosts
            }
        }
        // 3) fill in the last beat for snare/hat-like pads
        if (!anchor && len >= 8 && rng.uni() < amount * 0.45f) {
            for (int st = len - 4; st < len; ++st)
                if (at(p, st) == 0 && rng.uni() < 0.55f) at(p, st) = 0.45f + 0.4f * (float)(st - (len - 4)) / 4.0f;
        }
    }
}
