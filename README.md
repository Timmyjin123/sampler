# GrainPad

A fast pocket sampler for iPhone and iPad: 16 pads, hold-to-sample, a step sequencer with live recording, smart chop with tempo detection, one-tap pattern variations, master FX and a granular **Texture** mode on every pad.

## Run it on your iPhone

1. Download the repo (green **Code** button → **Download ZIP**) and unzip it, or `git clone` it.
2. Open `GrainPad.xcodeproj` in Xcode 16 or newer.
3. Click the **GrainPad** project in the left sidebar → target **GrainPad** → **Signing & Capabilities**.
   Pick your Apple ID under **Team** (Xcode → Settings → Accounts to add one; a free account works).
   If Xcode says the bundle identifier is taken, change `com.timmyjin123.grainpad` to anything unique.
4. Plug in the iPhone, choose it at the top of the window and press **Run** (⌘R).
5. First run only: on the iPhone open Settings → General → VPN & Device Management and trust your developer certificate.
   iOS 16+ also asks you to turn on **Developer Mode** (Settings → Privacy & Security).

A free Apple ID signs the app for 7 days; run it from Xcode again to refresh. With a paid developer account it lasts a year.

## How it works

The screen is a drum machine: pads at the bottom, one panel above them with three modes.

**SOUND** — the selected pad: waveform with trim handles, knobs (tone, envelope, texture, sends). Hold **REC** to sample the mic into the pad (auto-trimmed and normalized; a quick tap latches). **SOUNDS** drops any of the 112 kit sounds onto the pad. ✂ chops a loop: *smart* slices find the hits, spread them over the pads and write a pattern that replays the loop. Rarely used options (loop, reverse, mono, choke, tempo sync) live in pad settings (tap the pad name).

**SEQUENCE** — patterns A–H, 16 big steps for the selected pad (tap or drag to paint, GHOST for quiet hits). **NEW GROOVE 🎲** invents a rhythm in the chosen style; **LIBRARY** has 64 hand-written grooves in 9 styles (boom bap, trap & drill, house, techno, breaks & DnB, latin & afro, funk & soul, electro, chill). Grooves play on any kit — kick, snare, hats etc. are found by pad name. **VARIATE** makes four variations to try while it plays.

**FX** — hold 1/4–1/32 to stutter, **TO PAD** resamples the output into the selected pad, presets, DJ filter, drive, crush, delay, reverb.

**Library** (top right) — projects (new, open, duplicate, rename, delete), 7 kits (Boom Bap, Trap, House, Techno, Lo-Fi, Electro, Ambient), and export: the pattern as WAV (1 or 4 loops), as MIDI for an Ableton Drum Rack (pad 1 = C1), or all pad samples.

Projects save themselves to Files → GrainPad → Projects.

## Code

- `GrainPad/Engine` — the audio engine in C++ (no Apple APIs): voices, texture grains, sequencer, FX, tempo detection, chop, variations. `GPEngine.h` is the C interface.
- `GrainPad/Audio/GPAudio.mm` — AVAudioEngine bridge: low-latency session, microphone and resample capture, file I/O.
- `GrainPad/App`, `GrainPad/Views` — SwiftUI app; the pad grid is UIKit for direct multitouch.
- `tests/engine_test.cpp` — engine tests (timing, tempo detection, chop, variations, stability):
  `clang++ -std=c++17 -O2 -IGrainPad/Engine tests/engine_test.cpp GrainPad/Engine/*.cpp -o t && ./t`
- `tools/gen_xcodeproj.py` — regenerates the Xcode project after adding or removing files.

All engine commands go through one lock-free queue, so MIDI input can be added later without touching the audio thread.
