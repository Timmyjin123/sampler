import SwiftUI

struct TopBar: View {
    @EnvironmentObject var state: AppState
    @State private var confirmNew = false

    var body: some View {
        VStack(spacing: 8) {
            HStack(spacing: 6) {
                TransportButtons()
                BPMControl()
                metronome
                Spacer(minLength: 0)
                SampleRecordButton(source: .mic, compact: true)
                menu
            }
            HStack(spacing: 4) {
                PatternRow()
                Spacer(minLength: 4)
                Meter()
                    .frame(width: 44, height: 18)
            }
        }
        .confirmationDialog("Start a new empty project? The current one will be replaced.", isPresented: $confirmNew, titleVisibility: .visible) {
            Button("New empty project", role: .destructive) { state.newProject() }
            Button("Load starter kit", role: .destructive) { state.loadStarterKit() }
            Button("Cancel", role: .cancel) {}
        }
    }

    private var metronome: some View {
        let mode = Int(Eng.master(GP_M_METRONOME))
        return Button {
            Eng.setMaster(GP_M_METRONOME, Float((mode + 1) % 3)); state.changed()
            state.show(["Click off", "Click while recording", "Click always"][(mode + 1) % 3])
        } label: {
            Image(systemName: "metronome.fill")
                .font(.system(size: 13, weight: .semibold))
                .frame(width: 34, height: 30)
                .foregroundColor(mode == 0 ? Theme.faint : (mode == 1 ? Theme.accent : .black))
                .background(RoundedRectangle(cornerRadius: 7).fill(mode == 2 ? Theme.accent : Theme.raised))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Metronome")
    }

    private var menu: some View {
        Menu {
            Button { state.saveNow(); state.show("Saved") } label: { Label("Save now", systemImage: "square.and.arrow.down") }
            Button { confirmNew = true } label: { Label("New project…", systemImage: "doc.badge.plus") }
            Divider()
            Text(String(format: "Latency ≈ %.0f ms · %.0f kHz", state.audio.latencyMs, state.audio.sampleRate / 1000))
        } label: {
            Image(systemName: "line.3.horizontal")
                .font(.system(size: 14, weight: .semibold))
                .frame(width: 34, height: 30)
                .foregroundColor(Theme.text)
                .background(RoundedRectangle(cornerRadius: 7).fill(Theme.raised))
        }
    }
}

/// Drag up/down to change tempo, tap repeatedly to tap the tempo in.
struct BPMControl: View {
    @EnvironmentObject var state: AppState
    @State private var dragStart: Float? = nil
    @State private var taps: [Date] = []

    var body: some View {
        let bpm = Eng.master(GP_M_BPM)
        VStack(spacing: 0) {
            Text(String(format: "%.1f", bpm))
                .font(.system(size: 15, weight: .semibold, design: .monospaced))
                .foregroundColor(dragStart != nil ? Theme.accent : Theme.text)
            Text("BPM").font(.system(size: 8, weight: .bold, design: .rounded)).foregroundColor(Theme.faint)
        }
        .frame(width: 62, height: 30)
        .background(RoundedRectangle(cornerRadius: 7).fill(Theme.raised))
        .contentShape(Rectangle())
        .gesture(
            DragGesture(minimumDistance: 4)
                .onChanged { g in
                    if dragStart == nil { dragStart = bpm }
                    let v = (dragStart ?? bpm) - Float(g.translation.height) / 4 * 0.5
                    Eng.setMaster(GP_M_BPM, (min(max(v, 40), 240) * 2).rounded() / 2)
                }
                .onEnded { _ in dragStart = nil; state.changed() }
        )
        .simultaneousGesture(TapGesture().onEnded { tap() })
        .accessibilityLabel("Tempo \(Int(bpm)) BPM")
    }

    private func tap() {
        let now = Date()
        taps = taps.filter { now.timeIntervalSince($0) < 2.5 } + [now]
        guard taps.count >= 3 else { state.show("Tap \(4 - min(3, taps.count)) more…"); return }
        let recent = Array(taps.suffix(5))
        let intervals = zip(recent.dropFirst(), recent).map { $0.timeIntervalSince($1) }
        let avg = intervals.reduce(0, +) / Double(intervals.count)
        let bpm = Float(60 / avg)
        Eng.setMaster(GP_M_BPM, (min(max(bpm, 40), 240) * 2).rounded() / 2)
        state.changed()
    }
}

/// Stereo output meter.
struct Meter: View {
    @EnvironmentObject var ticker: Ticker
    var body: some View {
        let _ = ticker.frame
        VStack(spacing: 2) {
            ForEach(0..<2, id: \.self) { ch in
                let v = CGFloat(min(1, Eng.outputPeak(ch)))
                GeometryReader { g in
                    ZStack(alignment: .leading) {
                        Capsule().fill(Theme.raised)
                        Capsule().fill(v > 0.93 ? Theme.rec : (v > 0.7 ? Theme.accent : Theme.go))
                            .frame(width: g.size.width * v)
                    }
                }
            }
        }
    }
}

struct TransportButtons: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var ticker: Ticker

    var body: some View {
        let _ = ticker.frame
        let playing = Eng.isPlaying
        let recording = Eng.isRecording
        HStack(spacing: 6) {
            iconButton(playing ? "stop.fill" : "play.fill", tint: playing ? Theme.go : Theme.text, filled: playing) {
                state.togglePlay()
            }
            .accessibilityLabel(playing ? "Stop" : "Play")
            iconButton("record.circle", tint: recording ? .white : Theme.rec, filled: recording, fill: Theme.rec) {
                state.toggleRecordSequence()
            }
            .accessibilityLabel("Record pattern")
        }
    }

    private func iconButton(_ name: String, tint: Color, filled: Bool, fill: Color = Theme.go, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: name)
                .font(.system(size: 14, weight: .bold))
                .frame(width: 40, height: 30)
                .foregroundColor(filled ? (fill == Theme.go ? .black : .white) : tint)
                .background(RoundedRectangle(cornerRadius: 7).fill(filled ? fill : Theme.raised))
        }
        .buttonStyle(.plain)
    }
}

struct PatternRow: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var ticker: Ticker

    var body: some View {
        let _ = ticker.frame
        HStack(spacing: 4) {
            ForEach(0..<Eng.patternCount, id: \.self) { p in patternChip(p) }
        }
    }

    private func patternChip(_ p: Int) -> some View {
        let current = Eng.currentPattern == p
        let queued = Eng.queuedPattern == p
        let empty = Eng.patternIsEmpty(p)
        return Button { state.selectPattern(p) } label: {
            Text(state.patternName(p))
                .font(.system(size: 12, weight: .bold, design: .rounded))
                .frame(width: 30, height: 24)
                .foregroundColor(current ? .black : (empty ? Theme.faint : Theme.text))
                .background(
                    RoundedRectangle(cornerRadius: 6)
                        .fill(current ? Theme.accent : Theme.raised)
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(Theme.accent, lineWidth: queued ? 1.5 : 0)
                        .opacity(queued ? (ticker.frame % 16 < 8 ? 1 : 0.3) : 0)
                )
        }
        .buttonStyle(.plain)
    }

}
