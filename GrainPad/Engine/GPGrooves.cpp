// Groove library: ~50 drum patterns written for roles, plus an endless generator.
// 'x' = full hit, 'o' = medium, 'g' = ghost, '.' = rest.
#include "GPEngine.h"
#include "GPDSP.hpp"
#include <cstring>
#include <vector>

using namespace gp;

namespace {

enum { K = GP_R_KICK, S = GP_R_SNARE, C = GP_R_CLAP, H = GP_R_HAT, O = GP_R_OPEN, R = GP_R_RIM,
       P = GP_R_PERC, P2 = GP_R_PERC2, RD = GP_R_RIDE, B = GP_R_BASS };

const char* kGenres[] = {"Boom Bap", "Trap & Drill", "House", "Techno", "Breaks & DnB",
                         "Latin & Afro", "Funk & Soul", "Electro", "Chill"};

struct Row { int role; const char* steps; };
struct Groove {
    const char* name;
    int genre;
    float bpm, swing;
    std::vector<Row> rows;
};

const std::vector<Groove>& grooves() {
    static const std::vector<Groove> g = {
        // ------------------------------------------------ Boom Bap (0)
        {"Classic", 0, 92, 0.12f, {{K, "x.......x.x....."}, {S, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}}},
        {"Head Nod", 0, 90, 0.14f, {{K, "x......x..x....."}, {S, "....x.......x..."}, {H, "x.xgx.xgx.xgx.xg"}, {O, "..............o."}}},
        {"Lazy Swing", 0, 86, 0.18f, {{K, "x.....x...x..x.."}, {S, "....x.......x..g"}, {H, "x.x.x.x.x.x.x.x."}, {R, "...o......o....."}}},
        {"Drunk", 0, 88, 0.25f, {{K, "x..x......x..x.."}, {S, "....x.......x..."}, {H, "x.xx.xx.x.xx.x.x"}}},
        {"Golden Era", 0, 94, 0.1f, {{K, "x.......x...x..."}, {S, "....x.......x..."}, {H, "xgxgxgxgxgxgxgxg"}, {O, ".......o........"}}},
        {"Dusty Kicks", 0, 89, 0.15f, {{K, "x.x....x..x..x.."}, {S, "....x.......x..."}, {H, "x...x...x...x..."}, {P, "..o...o...o...o."}}},
        {"Rim Shot", 0, 93, 0.12f, {{K, "x......x.x......"}, {R, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {S, "...............g"}}},
        {"Back In The Day", 0, 96, 0.08f, {{K, "x.....x.x.x....."}, {S, "....x.......x..."}, {C, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {B, "x.....x...x....."}}},

        // ------------------------------------------------ Trap & Drill (1), half time over 32 steps
        {"Trap Basic", 1, 140, 0, {{K, "x.......x.x.............x......."}, {S, "........x...............x......."},
                                    {H, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x."}, {B, "x.......x.x.............x......."}}},
        {"Trap Rolls", 1, 142, 0, {{K, "x......x..x.....x.......x......."}, {S, "........x...............x......."},
                                    {H, "x.x.x.xxx.x.x.x.x.x.xxxx.x.x.xxx"}, {O, "..............................o."}, {B, "x......x..x.....x..............."}}},
        {"Bounce", 1, 138, 0, {{K, "x..x......x..x..x.....x...x....."}, {C, "........x...............x......."},
                                {H, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x."}, {P, "......o.......o.......o.......o."}, {B, "x..x......x.....x.....x........."}}},
        {"Drill", 1, 142, 0, {{K, "x.....x.........x..x............"}, {S, "........x.........x.....x......."},
                               {H, "x..x..x.x..x..x.x..x..x.x..x.x.x"}, {B, "x.....x.........x..x............"}}},
        {"Rage", 1, 150, 0, {{K, "x.x.....x.x.....x.x.....x.x.x..."}, {S, "........x...............x......."},
                              {H, "oooooooooooooooooooooooooooooooo"}, {B, "x.x.....x.x.....x.x.....x.x.x..."}}},
        {"Memphis", 1, 136, 0, {{K, "x..x..x...x.....x..x..x...x....."}, {S, "........x...............x......."},
                                 {H, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x."}, {P, "x...x...x...x...x...x...x...x..."}}},
        {"Dark Trap", 1, 130, 0, {{K, "x.........x.....x.....x........."}, {S, "........x...............x......."},
                                   {H, "x...x...x.x.x...x...x...x.xxx..."}, {O, "......o...............o........."}, {B, "x.........x.....x.....x........."}}},

        // ------------------------------------------------ House (2)
        {"Four On The Floor", 2, 124, 0.05f, {{K, "x...x...x...x..."}, {C, "....x.......x..."}, {O, "..x...x...x...x."}, {H, "x.x.x.x.x.x.x.x."}}},
        {"Deep", 2, 120, 0.1f, {{K, "x...x...x...x..."}, {C, "....x.......x..."}, {O, "..x...x...x...x."}, {P, "...x..x....x..x."}, {B, "..x...x...x...x."}}},
        {"Garage 2-Step", 2, 132, 0.2f, {{K, "x......x..x....."}, {S, "....x.......x..."}, {H, "..x...x...x.x.x."}, {P, "...o.o.....o..o."}, {B, "x......x..x....."}}},
        {"Disco", 2, 118, 0.04f, {{K, "x...x...x...x..."}, {S, "....x.......x..."}, {H, "xoxoxoxoxoxoxoxo"}, {O, "..x...x...x...x."}}},
        {"Jackin'", 2, 125, 0.1f, {{K, "x...x...x...x..."}, {C, "....x..x....x..."}, {H, "x.xxx.xxx.xxx.xx"}, {P, "..x..x...x..x..."}}},
        {"Tech House", 2, 126, 0.06f, {{K, "x...x...x...x..."}, {C, "....x.......x..."}, {O, "..x...x...x...x."}, {H, "x.x.x.x.x.x.x.x."}, {P, "....x.x.....x.x."}, {B, "..x..x..x.x...x."}}},
        {"Chicago", 2, 122, 0.08f, {{K, "x...x...x...x..."}, {C, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {RD, "x.x.x.x.x.x.x.x."}, {P, "...x.......x...."}}},
        {"Afro House", 2, 122, 0.08f, {{K, "x...x...x...x..."}, {R, "...x..x....x..x."}, {P, "x..x..x.x..x..x."}, {O, "..x...x...x...x."}, {P2, "......x.......x."}}},

        // ------------------------------------------------ Techno (3)
        {"Berlin", 3, 132, 0, {{K, "x...x...x...x..."}, {H, "x.xxx.xxx.xxx.xx"}, {O, "..x...x...x...x."}, {C, "....x.......x..."}, {P, "...x..x....x..x."}}},
        {"Industrial", 3, 136, 0, {{K, "x...x...x.x.x..."}, {C, "....x.......x..."}, {H, "xxxxxxxxxxxxxxxx"}, {P, "x..x..x..x..x..."}}},
        {"Rolling", 3, 130, 0, {{K, "x...x...x...x..."}, {B, ".xx..xx..xx..xx."}, {O, "..x...x...x...x."}, {RD, "x.x.x.x.x.x.x.x."}}},
        {"Minimal", 3, 126, 0.05f, {{K, "x...x...x...x..."}, {R, ".....x.....x..x."}, {O, "..x...x...x...x."}, {P, "...o.......o...."}}},
        {"Hardgroove", 3, 140, 0, {{K, "x...x...x...x..."}, {C, "....x.......x..."}, {H, "xxxxxxxxxxxxxxxx"}, {P, "x..x..x.x..x..x."}, {P2, "..x.......x..x.."}}},
        {"Acid", 3, 128, 0, {{K, "x...x...x...x..."}, {O, "..x...x...x...x."}, {C, "....x.......x..."}, {B, "x.xx.x.xx.x.xx.x"}}},
        {"Warehouse", 3, 134, 0, {{K, "x...x...x...x..."}, {S, "....x.......x..."}, {H, "..x...x...x...x."}, {RD, "xxxxxxxxxxxxxxxx"}, {P, ".x.....x.x.....x"}}},

        // ------------------------------------------------ Breaks & DnB (4)
        {"Funky Break", 4, 100, 0.08f, {{K, "x.x.......x..x.."}, {S, "....x..g.g..x..g"}, {H, "x.x.x.x.x.x.x.x."}, {O, ".......o........"}}},
        {"Break One", 4, 110, 0.05f, {{K, "x.x.......xx...."}, {S, "....x..g.g..x..g"}, {H, "x.x.x.x.x.x.x.x."}}},
        {"Big Beat", 4, 115, 0.05f, {{K, "x......x..x....."}, {S, "....x.......x..."}, {C, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {O, "......o.......o."}}},
        {"Drum & Bass", 4, 174, 0, {{K, "x.........x....."}, {S, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {B, "x.........x....."}}},
        {"Liquid", 4, 172, 0, {{K, "x.........x..x.."}, {S, "....x.......x..."}, {H, "x.xgx.xgx.xgx.xg"}, {RD, "x...x...x...x..."}}},
        {"Jungle", 4, 165, 0.04f, {{K, "x.x.......x....."}, {S, "....x..x.x..x.x."}, {H, "x.x.x.x.x.x.x.x."}, {P, "..o....o..o....o"}}},
        {"Halftime", 4, 170, 0, {{K, "x.........x....."}, {S, "........x......."}, {H, "x.x.x.x.x.x.x.x."}, {P, "......o.......o."}}},
        {"Footwork", 4, 160, 0, {{K, "x..x..x...x..x.."}, {S, "....x.......x..."}, {H, "gggggggggggggggg"}, {C, "...x..x.x..x..x."}}},

        // ------------------------------------------------ Latin & Afro (5)
        {"Dembow", 5, 95, 0, {{K, "x...x...x...x..."}, {S, "...x..x....x..x."}, {H, "x.x.x.x.x.x.x.x."}, {B, "x......x........"}}},
        {"Perreo", 5, 98, 0, {{K, "x...x...x...x..."}, {S, "...x..x....x..x."}, {C, "...x..x....x..x."}, {O, "..x...x...x...x."}}},
        {"Afrobeats", 5, 105, 0.06f, {{K, "x..x..x...x....."}, {S, "....x.......x..."}, {R, "...x..x....x..x."}, {H, "x.x.x.x.x.x.x.x."}, {P, "x..x..x...x.x..."}}},
        {"Amapiano", 5, 113, 0.08f, {{K, "x...x...x...x..."}, {B, "..x...x..x.x..x."}, {R, "...x.....x...x.."}, {H, "xgxgxgxgxgxgxgxg"}, {C, "....x.......x..."}}},
        {"Baile Funk", 5, 130, 0, {{K, "x..x...x..x..x.."}, {S, "..x..x....x..x.."}, {H, "x.x.x.x.x.x.x.x."}}},
        {"Bossa", 5, 130, 0, {{K, "x..x..x.x..x..x."}, {R, "x..x..x...x..x.."}, {H, "x.x.x.x.x.x.x.x."}}},
        {"Clave", 5, 108, 0.05f, {{R, "x..x..x...x.x..."}, {K, "x.......x......."}, {P, "..x...x...x...x."}, {H, "x.x.x.x.x.x.x.x."}}},
        {"Highlife", 5, 118, 0.06f, {{K, "x.....x...x....."}, {R, "x.x.x..x.x.x.x.."}, {P, "..x...x...x...x."}, {S, "....x.......x..."}}},

        // ------------------------------------------------ Funk & Soul (6)
        {"Funk", 6, 104, 0.06f, {{K, "x.x...x...x..x.."}, {S, "....x..g.g..x..."}, {H, "xgxgxgxgxgxgxgxg"}, {O, "..............o."}}},
        {"Soul", 6, 96, 0.1f, {{K, "x.....x.x......."}, {S, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {P, "x...x...x...x..."}}},
        {"New Jack Swing", 6, 105, 0.25f, {{K, "x..x..x.x..x...."}, {S, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}}},
        {"Motown", 6, 120, 0.04f, {{K, "x.......x......."}, {S, "....x.......x..."}, {C, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {P, "....x.......x..."}}},
        {"Pocket", 6, 92, 0.15f, {{K, "x......x.x....x."}, {S, "....x..g....x.g."}, {H, "x.xgx.xgx.xgx.xg"}}},
        {"Shuffle", 6, 110, 0.33f, {{K, "x.....x.x.....x."}, {S, "....x.......x..."}, {H, "x.xx.xx.xx.xx.x."}}},

        // ------------------------------------------------ Electro (7)
        {"Electro Funk", 7, 118, 0, {{K, "x.....x...x....."}, {S, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {P, "..x....x..x....."}}},
        {"Miami Bass", 7, 128, 0, {{K, "x......x..x...x."}, {S, "....x.......x..."}, {H, "xxxxxxxxxxxxxxxx"}, {P, "..x.......x....."}, {B, "x......x..x...x."}}},
        {"Synth Pop", 7, 120, 0, {{K, "x...x...x...x..."}, {S, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {C, "....x.......x..."}}},
        {"Jersey Club", 7, 140, 0, {{K, "x...x...x.x.x.x."}, {S, "....x.......x..."}, {P, "x..x..x..x..x..x"}, {H, "..x...x...x...x."}}},
        {"Robot", 7, 112, 0, {{K, "x..x....x.x....."}, {C, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {P, "......x.......x."}, {P2, ".x.......x......"}}},
        {"Breakdance", 7, 116, 0.04f, {{K, "x.....x...x...x."}, {S, "....x.......x..."}, {O, "..x...x...x...x."}, {P, "x..x..x..x..x..."}}},

        // ------------------------------------------------ Chill (8)
        {"Lo-Fi", 8, 80, 0.2f, {{K, "x.......x.x....."}, {S, "....x.......x..."}, {H, "x.xgx.xgx.xgx.xg"}, {P, "gxgxgxgxgxgxgxgx"}}},
        {"Rainy Day", 8, 76, 0.22f, {{K, "x......x..x....."}, {S, "....x.......x..g"}, {H, "x...x...x...x..."}, {R, "......o.......o."}}},
        {"Dub", 8, 75, 0.1f, {{K, "........x......."}, {S, "........x......."}, {R, "..x...x...x...x."}, {H, "x.x.x.x.x.x.x.x."}}},
        {"Trip Hop", 8, 84, 0.12f, {{K, "x......x..x....."}, {S, "....x.......x..."}, {H, "x.x.x.x.x.x.x.x."}, {O, "...........o...."}, {P, "......o.......o."}}},
        {"Downtempo", 8, 95, 0.08f, {{K, "x.....x...x....."}, {S, "....x.......x..."}, {H, "xgxgxgxgxgxgxgxg"}, {P, "...o.......o...."}}},
        {"Sparse", 8, 70, 0.05f, {{K, "x.........x....."}, {R, "....o.......o..."}, {P, "..g...g...g...g."}}},
    };
    return g;
}

float cellValue(char c) { return c == 'x' ? 1.0f : c == 'o' ? 0.65f : c == 'g' ? 0.35f : 0.0f; }
int rowLen(const char* s) { int n = (int)std::strlen(s); return n > GP_MAX_STEPS ? GP_MAX_STEPS : n; }
bool patOk(int p) { return p >= 0 && p < GP_NUM_PATTERNS; }

// Roles that fall back to another role's pad when the kit lacks them.
int padFor(int role, const int* rolePads) {
    if (!rolePads) return -1;
    int p = rolePads[role];
    if (p >= 0 && p < GP_NUM_PADS) return p;
    switch (role) {
        case GP_R_CLAP: return padFor(GP_R_SNARE, rolePads);
        case GP_R_SNARE: p = rolePads[GP_R_CLAP]; return (p >= 0 && p < GP_NUM_PADS) ? p : -1;
        case GP_R_OPEN: return padFor(GP_R_HAT, rolePads);
        case GP_R_RIDE: return padFor(GP_R_OPEN, rolePads);
        case GP_R_RIM: return padFor(GP_R_PERC, rolePads);
        case GP_R_PERC2: return padFor(GP_R_PERC, rolePads);
        default: return -1;
    }
}

void writeRow(int pattern, int pad, const char* steps, int len) {
    int n = rowLen(steps);
    for (int i = 0; i < len; ++i) {
        float v = cellValue(steps[i % n]);
        if (v > 0 && v > gp_get_step(pattern, pad, i)) gp_set_step(pattern, pad, i, v);
    }
}
} // namespace

int gp_genre_count(void) { return (int)(sizeof(kGenres) / sizeof(kGenres[0])); }
const char* gp_genre_name(int g) { return (g >= 0 && g < gp_genre_count()) ? kGenres[g] : ""; }
int gp_groove_count(void) { return (int)grooves().size(); }
const char* gp_groove_name(int i) { return (i >= 0 && i < gp_groove_count()) ? grooves()[i].name : ""; }
int gp_groove_genre(int i) { return (i >= 0 && i < gp_groove_count()) ? grooves()[i].genre : -1; }
float gp_groove_bpm(int i) { return (i >= 0 && i < gp_groove_count()) ? grooves()[i].bpm : 0.0f; }
float gp_groove_swing(int i) { return (i >= 0 && i < gp_groove_count()) ? grooves()[i].swing : 0.0f; }

void gp_apply_groove(int i, int pattern, const int* rolePads) {
    if (i < 0 || i >= gp_groove_count() || !patOk(pattern)) return;
    const Groove& g = grooves()[i];
    int len = 16;
    for (auto& r : g.rows) len = std::max(len, rowLen(r.steps));
    gp_clear_pattern(pattern);
    gp_set_pattern_length(pattern, len);
    for (auto& r : g.rows) {
        int pad = padFor(r.role, rolePads);
        if (pad >= 0) writeRow(pattern, pad, r.steps, len);
    }
}

float gp_generate_groove(int genre, unsigned seed, float wildness, int pattern, const int* rolePads, float* swing) {
    if (!patOk(pattern)) return 0.0f;
    Rng rng(seed * 2246822519u + 3266489917u);
    std::vector<int> pool;
    for (int i = 0; i < gp_groove_count(); ++i) if (genre < 0 || grooves()[i].genre == genre) pool.push_back(i);
    if (pool.empty()) return 0.0f;

    // the base groove sets length, tempo and swing; other roles may come from siblings
    const Groove& base = grooves()[pool[rng.next() % pool.size()]];
    int len = 16;
    for (auto& r : base.rows) len = std::max(len, rowLen(r.steps));
    gp_clear_pattern(pattern);
    gp_set_pattern_length(pattern, len);

    for (int role = 0; role < GP_R_COUNT; ++role) {
        const char* steps = nullptr;
        for (auto& r : base.rows) if (r.role == role) steps = r.steps;
        if (rng.uni() < 0.55f) {   // borrow this role from another groove of the genre
            std::vector<const char*> options;
            for (int gi : pool)
                for (auto& r : grooves()[gi].rows)
                    if (r.role == role && (rowLen(r.steps) == len || (rowLen(r.steps) == 16 && len == 32))) options.push_back(r.steps);
            if (!options.empty()) steps = options[rng.next() % options.size()];
        }
        if (!steps) continue;
        // the kick and snare carry the feel: keep them, but allow other roles to drop out
        if (role != GP_R_KICK && role != GP_R_SNARE && role != GP_R_HAT && rng.uni() < 0.2f) continue;
        int pad = padFor(role, rolePads);
        if (pad >= 0) writeRow(pattern, pad, steps, len);
    }

    // mutate
    float amount = clampf(wildness, 0.0f, 1.0f);
    if (amount > 0.01f) {
        std::vector<float> out(GP_NUM_PADS * GP_MAX_STEPS);
        gp_make_variation(pattern, rng.next(), amount * 0.6f, out.data());
        gp_apply_steps(pattern, out.data());
    }
    if (swing) *swing = clampf(base.swing + (rng.uni() - 0.5f) * 0.06f * amount, 0.0f, 0.4f);
    return std::round(base.bpm + (rng.uni() - 0.5f) * 8.0f * amount);
}
