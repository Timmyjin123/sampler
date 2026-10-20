import SwiftUI
import QuartzCore

/// 30 fps heartbeat for views that show live engine state (playheads, step cursor, meters).
/// Kept separate from AppState so the rest of the UI doesn't redraw every frame.
@MainActor
final class Ticker: ObservableObject {
    @Published private(set) var frame: UInt64 = 0
    private var link: CADisplayLink?

    init() {
        let link = CADisplayLink(target: TickerProxy(self), selector: #selector(TickerProxy.tick))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 20, maximum: 30, preferred: 30)
        link.add(to: .main, forMode: .common)
        self.link = link
    }

    fileprivate func tick() { frame &+= 1 }
}

/// Breaks the retain cycle between CADisplayLink and its target.
@MainActor
private final class TickerProxy: NSObject {
    weak var owner: Ticker?
    init(_ owner: Ticker) { self.owner = owner }
    @objc func tick() { owner?.tick() }
}
