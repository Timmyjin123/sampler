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

**Pads.** Tap to play (multitouch). The last pad you touched is the one you edit.

**Sampling.** Hold **REC** to record from the microphone into the selected pad and let go to keep it. The take is trimmed and normalized automatically. A quick tap latches recording; tap again to stop. **RESAMPLE** does the same with the app's own output — play your beat and bounce it to a pad. **IMPORT** loads any audio file from Files.

**Smart chop.** **CHOP → 16 slices** finds the hits in a loop, spreads them over the pads and writes a pattern that plays the loop back. Then press **VARIATE** in STEPS to remix it.

**Tempo.** Long samples get their tempo detected. **REPITCH** follows the project tempo like a turntable; **STRETCH** keeps the pitch. The metronome button next to it sets the project to the sample's tempo.

**Steps.** 8 patterns (A–H), 16–64 steps. Tap or drag across cells to paint, **GHOST** for quiet hits. With ● pressed and the transport running, pads you play are recorded and quantized. Patterns switch at the end of the bar.

**Variate.** Makes four variations of the current pattern (slice shuffles, ghost notes, fills, drops). Try V1–V4 while it plays, ↩ goes back, **KEEP** commits. **WILD** sets how far they go.

**Texture.** Turns a pad into a granular cloud: hold it for drones and pads. SIZE, DENSITY, SPRAY, SCAN (0 = freeze), SCATTER (random pitch), WIDTH.

**FX.** Hold 1/4–1/32 to stutter. DJ filter (left = low-pass, right = high-pass), drive, bitcrush, synced ping-pong delay and reverb. Each pad has its own delay/reverb send.

The project saves itself (Files app → GrainPad → Project), including all samples as WAV.

## Code

- `GrainPad/Engine` — the audio engine in C++ (no Apple APIs): voices, texture grains, sequencer, FX, tempo detection, chop, variations. `GPEngine.h` is the C interface.
- `GrainPad/Audio/GPAudio.mm` — AVAudioEngine bridge: low-latency session, microphone and resample capture, file I/O.
- `GrainPad/App`, `GrainPad/Views` — SwiftUI app; the pad grid is UIKit for direct multitouch.
- `tests/engine_test.cpp` — engine tests (timing, tempo detection, chop, variations, stability):
  `clang++ -std=c++17 -O2 -IGrainPad/Engine tests/engine_test.cpp GrainPad/Engine/*.cpp -o t && ./t`
- `tools/gen_xcodeproj.py` — regenerates the Xcode project after adding or removing files.

All engine commands go through one lock-free queue, so MIDI input can be added later without touching the audio thread.
