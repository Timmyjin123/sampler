import SwiftUI

/// Hold to record into the selected pad, release to keep it.
/// A quick tap latches recording on; the next tap stops it.
struct SampleRecordButton: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var ticker: Ticker
    let source: GPRecordSource
    var compact = false
    @State private var wasLatched = false

    private var active: Bool { state.recordingSource == source }
    private var title: String { source == .mic ? "REC" : "RESAMPLE" }
    private var icon: String { source == .mic ? "mic.fill" : "arrow.triangle.2.circlepath" }

    var body: some View {
        HoldButton(onPress: press, onRelease: release) { pressed in
            let _ = ticker.frame
            HStack(spacing: 5) {
                Image(systemName: icon).font(.system(size: compact ? 12 : 12, weight: .bold))
                if !compact {
                    Text(active ? String(format: "%.1fs", state.audio.recordedSeconds) : title)
                        .font(Theme.label)
                        .monospacedDigit()
                }
            }
            .padding(.horizontal, compact ? 0 : 10)
            .frame(width: compact ? 34 : nil, height: compact ? 30 : 28)
            .foregroundColor(active ? .white : (source == .mic ? Theme.rec : Theme.text))
            .background(
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 7).fill(active ? Theme.rec : Theme.raised)
                    if active {
                        RoundedRectangle(cornerRadius: 7)
                            .fill(Color.white.opacity(0.25))
                            .scaleEffect(x: CGFloat(min(1, state.audio.recordLevel * 1.4)), y: 1, anchor: .leading)
                    }
                }
            )
            .overlay(RoundedRectangle(cornerRadius: 7).stroke(state.recordLatched && active ? Color.white : .clear, lineWidth: 1.5))
            .scaleEffect(pressed ? 0.95 : 1)
        }
        .accessibilityLabel(source == .mic ? "Record sample" : "Resample output")
    }

    private func press() {
        wasLatched = state.recordLatched
        if !wasLatched && state.recordingSource == nil { state.beginRecord(source) }
    }

    private func release(_ held: TimeInterval) {
        if wasLatched || held >= 0.3 { state.endRecord() }
        else { state.recordLatched = true }     // quick tap: keep recording until the next tap
        wasLatched = false
    }
}
