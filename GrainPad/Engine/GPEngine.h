// GrainPad engine — plain C interface.
// Swift calls this through the bridging header; the audio bridge (GPAudio.mm)
// calls gp_prepare / gp_render from the audio thread.
//
// Threading:
//  * gp_render runs on the audio thread and never locks or allocates.
//  * Everything else is meant for the main thread. Pad / master parameters are
//    atomics, so setting them is instant. Triggers, transport changes and sample
//    swaps go through a lock-free command queue that the audio thread drains at
//    the start of every block. External sources (MIDI later) use the same queue.

#ifndef GP_ENGINE_H
#define GP_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#define GP_NUM_PADS 16
#define GP_NUM_PATTERNS 8
#define GP_MAX_STEPS 64

typedef enum {
    GP_P_GAIN = 0,      // 0..1   (0.8 default, maps to -inf..+6 dB)
    GP_P_PAN,           // -1..1
    GP_P_PITCH,         // semitones -24..24
    GP_P_FINE,          // cents -100..100
    GP_P_START,         // 0..1 of sample
    GP_P_END,           // 0..1 of sample
    GP_P_ATTACK,        // seconds 0..2
    GP_P_DECAY,         // seconds 0..4
    GP_P_SUSTAIN,       // 0..1
    GP_P_RELEASE,       // seconds 0..4
    GP_P_CUTOFF,        // 0..1 (1 = open)
    GP_P_RESO,          // 0..1
    GP_P_REVERSE,       // 0/1
    GP_P_LOOP,          // 0/1
    GP_P_ONESHOT,       // 0/1 (1 = ignore note-off)
    GP_P_MONO,          // 0/1 (retrigger cuts previous voice of this pad)
    GP_P_CHOKE,         // 0 = none, 1..4 choke group
    GP_P_MODE,          // 0 = classic, 1 = texture (granular)
    GP_P_GRAIN_SIZE,    // seconds 0.01..0.5
    GP_P_GRAIN_DENSITY, // grains per second 2..80
    GP_P_GRAIN_SPRAY,   // 0..1
    GP_P_GRAIN_SCAN,    // playhead speed 0..2 (1 = original speed, 0 = frozen)
    GP_P_GRAIN_PITCHRND,// semitones 0..12
    GP_P_GRAIN_STEREO,  // 0..1
    GP_P_DELAY_SEND,    // 0..1
    GP_P_REVERB_SEND,   // 0..1
    GP_P_MUTE,          // 0/1
    GP_P_SRC_BPM,       // detected/entered tempo of the sample, 0 = unknown
    GP_P_SYNC,          // 0 = off, 1 = repitch to project tempo, 2 = stretch (keeps pitch)
    GP_PAD_PARAM_COUNT
} GPPadParam;

typedef enum {
    GP_M_VOLUME = 0,    // 0..1
    GP_M_FILTER,        // 0..1, 0.5 = bypass, <0.5 low-pass, >0.5 high-pass
    GP_M_RESO,          // 0..1
    GP_M_DRIVE,         // 0..1
    GP_M_CRUSH,         // 0..1
    GP_M_DELAY_TIME,    // 0..7 index: 1/32, 1/16, 3/32, 1/8, 3/16, 1/4, 3/8, 1/2
    GP_M_DELAY_FB,      // 0..0.95
    GP_M_DELAY_TONE,    // 0..1
    GP_M_REVERB_SIZE,   // 0..1
    GP_M_REVERB_DAMP,   // 0..1
    GP_M_REPEAT,        // 0 = off, 1 = 1/4, 2 = 1/8, 3 = 1/16, 4 = 1/32 (hold to stutter)
    GP_M_BPM,           // 40..240
    GP_M_SWING,         // 0..0.5
    GP_M_METRONOME,     // 0 = off, 1 = while recording, 2 = always
    GP_M_COUNT
} GPMasterParam;

// ---- lifecycle (audio bridge) ----
void  gp_prepare(double sampleRate, int maxFrames);
void  gp_render(float* left, float* right, int frames);
double gp_sample_rate(void);

// ---- parameters ----
void  gp_set_pad_param(int pad, int param, float value);
float gp_get_pad_param(int pad, int param);
void  gp_reset_pad_params(int pad);
void  gp_set_master(int param, float value);
float gp_get_master(int param);

// ---- performance ----
void  gp_trigger(int pad, float velocity);   // velocity 0..1
void  gp_release(int pad);
void  gp_all_notes_off(void);

// ---- transport / sequencer ----
void  gp_set_playing(int on);
int   gp_is_playing(void);
void  gp_set_recording(int on);              // live record into the current pattern
int   gp_is_recording(void);
void  gp_select_pattern(int pattern);        // queued to the pattern end while playing
int   gp_current_pattern(void);
int   gp_queued_pattern(void);               // -1 if nothing is queued
int   gp_current_step(void);                 // -1 when stopped
void  gp_set_pattern_length(int pattern, int steps);
int   gp_get_pattern_length(int pattern);
void  gp_set_step(int pattern, int pad, int step, float velocity); // 0 = off
float gp_get_step(int pattern, int pad, int step);
void  gp_clear_pattern(int pattern);
void  gp_clear_pad_steps(int pattern, int pad);
void  gp_copy_pattern(int from, int to);

// ---- samples (main thread) ----
// Copies the audio. right may be NULL for mono. Returns frames actually kept.
// autoTrim cuts leading/trailing silence, normalize brings the peak to -1 dBFS.
int   gp_load_pad_sample(int pad, const float* left, const float* right, int frames,
                         double sampleRate, int autoTrim, int normalize);
void  gp_share_pad_sample(int dstPad, int srcPad);   // same audio, own parameters
void  gp_clear_pad_sample(int pad);
int   gp_pad_has_sample(int pad);
int   gp_pad_frames(int pad);
int   gp_pad_channels(int pad);
double gp_pad_sample_rate(int pad);
long  gp_pad_sample_id(int pad);                     // same id = shared audio
int   gp_pad_copy_audio(int pad, float* left, float* right, int maxFrames);
// min/max pairs over the whole sample: out must hold bins*2 floats.
void  gp_pad_peaks(int pad, float* out, int bins);
void  gp_collect_garbage(void);                      // call regularly on main thread

// ---- analysis / smart tools (main thread) ----
// Tempo of the pad's sample (whole sample, ignores trim). 0 if not rhythmic.
float gp_detect_bpm(int pad);
// Smart chop: slices the pad's sample on transients (or equally when
// equalSlices = 1) onto pads firstPad.. (wraps), up to maxSlices.
// If writePattern >= 0 and the tempo is known, that pattern is rewritten so
// the slices replay the original loop. Returns number of slices.
int   gp_smart_chop(int pad, int firstPad, int maxSlices, int equalSlices, int writePattern);
// Pattern variation: fills out[GP_NUM_PADS * GP_MAX_STEPS] with velocities.
// amount 0..1 = how far from the source. Deterministic for a given seed.
void  gp_make_variation(int pattern, unsigned seed, float amount, float* out);
void  gp_apply_steps(int pattern, const float* steps);   // writes a full pattern
void  gp_read_steps(int pattern, float* out);            // reads a full pattern

// Fills all pads with a synthesized starter kit and a demo beat in pattern 0.
void  gp_load_starter_kit(void);
const char* gp_starter_kit_name(int pad);

// ---- feedback for the UI (cheap to poll) ----
float gp_pad_activity(int pad);      // 0..1 envelope of the pad's voices
float gp_pad_playhead(int pad);      // 0..1 of the sample, -1 when silent
float gp_output_peak(int channel);   // 0..1, decays
unsigned gp_pad_hit_counter(int pad);// increments on every trigger (sequencer too)

#ifdef __cplusplus
}
#endif
#endif
