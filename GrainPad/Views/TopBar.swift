import SwiftUI

/// One slim row: transport, tempo, current pattern, project.
struct TopBar: View {
    @EnvironmentObject var state: AppState

    var body: some View {
        HStack(spacing: 6) {
            TransportButtons()
            BPMControl()
            PatternChip()
            Spacer(minLength: 4)
            Button { state.showLibrary = true } label: {
                HStack(spacing: 6) {
                    Text(state.projectName)
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .lineLimit(1)
                        .frame(maxWidth: 110, alignment: .trailing)
                    Image(systemName: "square.grid.2x2.fill").font(.system(size: 13, weight: .semibold))
                }
                .padding(.horizontal, 10)
                .frame(height: 34)
                .foregroundColor(Theme.text)
                .background(RoundedRectangle(cornerRadius: 9).fill(Theme.raised))
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Library: projects, kits and export")
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
            Button { state.togglePlay() } label: {
                Image(systemName: playing ? "stop.fill" : "play.fill")
                    .font(.system(size: 15, weight: .bold))
                    .frame(width: 44, height: 34)
                    .foregroundColor(playing ? .black : Theme.text)
                    .background(RoundedRectangle(cornerRadius: 9).fill(playing ? Theme.go : Theme.raised))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(playing ? "Stop" : "Play")
            Button { state.toggleRecordSequence() } label: {
                Circle()
                    .fill(recording ? Color.white : Theme.rec)
                    .frame(width: 13, height: 13)
                    .frame(width: 40, height: 34)
                    .background(RoundedRectangle(cornerRadius: 9).fill(recording ? Theme.rec : Theme.raised))
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Record into the pattern")
        }
    }
}

/// Current pattern; tap to jump to the sequencer.
struct PatternChip: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var ticker: Ticker

    var body: some View {
        let _ = ticker.frame
        let queued = Eng.queuedPattern
        Button { state.mode = .sequence } label: {
            HStack(spacing: 3) {
                Text(state.patternName(Eng.currentPattern))
                    .font(.system(size: 15, weight: .heavy, design: .rounded))
                if queued >= 0 {
                    Image(systemName: "arrow.right").font(.system(size: 9, weight: .bold))
                    Text(state.patternName(queued)).font(.system(size: 13, weight: .bold, design: .rounded))
                }
            }
            .frame(minWidth: 34).padding(.horizontal, 4)
            .frame(height: 34)
            .foregroundColor(.black)
            .background(RoundedRectangle(cornerRadius: 9).fill(Theme.accent))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Pattern \(state.patternName(Eng.currentPattern))")
    }
}

/// Drag up/down to change tempo, tap repeatedly to tap it in.
struct BPMControl: View {
    @EnvironmentObject var state: AppState
    @State private var dragStart: Float? = nil
    @State private var taps: [Date] = []

    var body: some View {
        let _ = state.revision
        let bpm = Eng.master(GP_M_BPM)
        VStack(spacing: -1) {
            Text(String(format: "%.0f", bpm))
                .font(.system(size: 16, weight: .semibold, design: .monospaced))
                .foregroundColor(dragStart != nil ? Theme.accent : Theme.text)
            Text("BPM").font(.system(size: 7, weight: .bold, design: .rounded)).foregroundColor(Theme.faint)
        }
        .frame(width: 52, height: 34)
        .background(RoundedRectangle(cornerRadius: 9).fill(Theme.raised))
        .contentShape(Rectangle())
        .gesture(
            DragGesture(minimumDistance: 4)
                .onChanged { g in
                    if dragStart == nil { dragStart = bpm }
                    let v = (dragStart ?? bpm) - Float(g.translation.height) / 3
                    Eng.setMaster(GP_M_BPM, min(max(v, 40), 240).rounded())
                    state.revision &+= 1
                }
                .onEnded { _ in dragStart = nil; state.changed() }
        )
        .simultaneousGesture(TapGesture().onEnded { tap() })
        .accessibilityLabel("Tempo \(Int(bpm)) BPM. Drag to change, tap to tap tempo")
    }

    private func tap() {
        let now = Date()
        taps = taps.filter { now.timeIntervalSince($0) < 2.5 } + [now]
        guard taps.count >= 3 else { state.show("Tap tempo: keep tapping…"); return }
        let recent = Array(taps.suffix(5))
        let intervals = zip(recent.dropFirst(), recent).map { $0.timeIntervalSince($1) }
        let avg = intervals.reduce(0, +) / Double(intervals.count)
        Eng.setMaster(GP_M_BPM, min(max(Float(60 / avg), 40), 240).rounded())
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
