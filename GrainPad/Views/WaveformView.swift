import SwiftUI

/// Caches waveform overviews per sample (samples are immutable once loaded).
@MainActor
final class PeakCache {
    static let shared = PeakCache()
    private var cache: [Int: [Float]] = [:]
    func peaks(pad: Int, bins: Int = 360) -> [Float] {
        let id = Eng.sampleID(pad)
        guard id != 0 else { return [] }
        if let p = cache[id] { return p }
        let p = Eng.peaks(pad, bins: bins)
        if cache.count > 64 { cache.removeAll() }
        cache[id] = p
        return p
    }
}

/// Waveform with draggable start/end handles and a live playhead.
struct WaveformView: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var ticker: Ticker
    let pad: Int
    @State private var dragging: GPPadParam? = nil

    var body: some View {
        let _ = ticker.frame
        let color = Theme.padColor(state.pads[pad].color)
        let peaks = PeakCache.shared.peaks(pad: pad)
        let start = CGFloat(Eng.pad(pad, GP_P_START))
        let end = CGFloat(Eng.pad(pad, GP_P_END))
        let head = Eng.playhead(pad)

        GeometryReader { geo in
            ZStack {
                RoundedRectangle(cornerRadius: 10).fill(Theme.raised.opacity(0.6))
                if peaks.isEmpty {
                    VStack(spacing: 4) {
                        Image(systemName: "waveform").font(.system(size: 20)).foregroundColor(Theme.faint)
                        Text("Hold REC to sample, or Import")
                            .font(Theme.label).foregroundColor(Theme.dim)
                    }
                } else {
                    Canvas { ctx, size in
                        let bins = peaks.count / 2
                        let mid = size.height / 2
                        let bw = size.width / CGFloat(bins)
                        let sx = start * size.width, ex = end * size.width
                        for b in 0..<bins {
                            let x = CGFloat(b) * bw
                            let mn = CGFloat(peaks[b * 2]), mx = CGFloat(peaks[b * 2 + 1])
                            let top = mid - mx * mid * 0.92
                            let bot = mid - mn * mid * 0.92
                            let inside = x + bw >= sx && x <= ex
                            let r = CGRect(x: x, y: top, width: max(1, bw - 0.6), height: max(1, bot - top))
                            ctx.fill(Path(r), with: .color(inside ? color : Theme.faint.opacity(0.6)))
                        }
                        // shade trimmed-away parts
                        ctx.fill(Path(CGRect(x: 0, y: 0, width: sx, height: size.height)), with: .color(Color.black.opacity(0.35)))
                        ctx.fill(Path(CGRect(x: ex, y: 0, width: size.width - ex, height: size.height)), with: .color(Color.black.opacity(0.35)))
                        // handles
                        for (x, isStart) in [(sx, true), (ex, false)] {
                            ctx.fill(Path(CGRect(x: x - 1, y: 0, width: 2, height: size.height)), with: .color(Theme.text))
                            let tab = CGRect(x: isStart ? x : x - 12, y: 0, width: 12, height: 16)
                            ctx.fill(Path(roundedRect: tab, cornerRadius: 3), with: .color(Theme.text))
                        }
                        if head >= 0 {
                            let hx = CGFloat(head) * size.width
                            ctx.fill(Path(CGRect(x: hx - 0.75, y: 0, width: 1.5, height: size.height)), with: .color(.white))
                        }
                    }
                    .padding(.vertical, 6)
                }
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        guard !peaks.isEmpty, geo.size.width > 0 else { return }
                        let x = min(max(g.location.x / geo.size.width, 0), 1)
                        if dragging == nil {
                            let s0 = g.startLocation.x / geo.size.width
                            dragging = abs(s0 - start) <= abs(s0 - end) ? GP_P_START : GP_P_END
                        }
                        guard let which = dragging else { return }
                        if which == GP_P_START {
                            Eng.setPad(pad, GP_P_START, Float(min(x, end - 0.002)))
                        } else {
                            Eng.setPad(pad, GP_P_END, Float(max(x, start + 0.002)))
                        }
                    }
                    .onEnded { _ in
                        if dragging != nil { state.changed() }
                        dragging = nil
                    }
            )
        }
    }
}
