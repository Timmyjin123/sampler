import SwiftUI
import UIKit

/// 4×4 multitouch pad grid. UIKit handles the touches directly: touch-down
/// triggers immediately, each finger keeps its pad until it lifts.
struct PadGrid: UIViewRepresentable {
    let metas: [PadMeta]
    let selected: Int
    let onPress: (Int) -> Void
    let onRelease: (Int) -> Void

    func makeUIView(context: Context) -> PadGridUIView {
        let v = PadGridUIView()
        v.onPress = onPress
        v.onRelease = onRelease
        v.update(metas: metas, selected: selected)
        return v
    }

    func updateUIView(_ v: PadGridUIView, context: Context) {
        v.onPress = onPress
        v.onRelease = onRelease
        v.update(metas: metas, selected: selected)
    }
}

final class PadGridUIView: UIView {
    var onPress: ((Int) -> Void)?
    var onRelease: ((Int) -> Void)?

    private let count = 16
    private var pads: [CALayer] = []
    private var fills: [CALayer] = []
    private var names: [CATextLayer] = []
    private var numbers: [CATextLayer] = []
    private var plus: [CATextLayer] = []
    private var metas: [PadMeta] = []
    private var selected = 0
    private var hasSample = [Bool](repeating: false, count: 16)
    private var touchPad: [ObjectIdentifier: Int] = [:]
    private var lastHits = [UInt32](repeating: 0, count: 16)
    private var flash = [CGFloat](repeating: 0, count: 16)
    private var held = [Int](repeating: 0, count: 16)
    private var link: CADisplayLink?

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        backgroundColor = .clear
        for i in 0..<count {
            let pad = CALayer()
            pad.cornerRadius = 10
            pad.cornerCurve = .continuous
            pad.borderWidth = 1.5
            pad.masksToBounds = true
            let fill = CALayer()
            pad.addSublayer(fill)
            let name = CATextLayer()
            name.fontSize = 11
            name.font = UIFont.systemFont(ofSize: 11, weight: .semibold)
            name.contentsScale = UIScreen.main.scale
            name.truncationMode = .end
            name.alignmentMode = .left
            pad.addSublayer(name)
            let num = CATextLayer()
            num.fontSize = 9
            num.font = UIFont.monospacedSystemFont(ofSize: 9, weight: .medium)
            num.contentsScale = UIScreen.main.scale
            num.alignmentMode = .right
            num.string = "\(i + 1)"
            pad.addSublayer(num)
            let p = CATextLayer()
            p.fontSize = 22
            p.font = UIFont.systemFont(ofSize: 22, weight: .light)
            p.contentsScale = UIScreen.main.scale
            p.alignmentMode = .center
            p.string = "+"
            pad.addSublayer(p)
            layer.addSublayer(pad)
            pads.append(pad); fills.append(fill); names.append(name); numbers.append(num); plus.append(p)
        }
    }

    required init?(coder: NSCoder) { fatalError() }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        link?.invalidate()
        link = nil
        if window != nil {
            let l = CADisplayLink(target: LinkProxy(self), selector: #selector(LinkProxy.tick))
            l.add(to: .main, forMode: .common)
            link = l
        }
    }

    func update(metas: [PadMeta], selected: Int) {
        self.metas = metas
        self.selected = selected
        for i in 0..<count { hasSample[i] = Eng.hasSample(i) }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for i in 0..<count { style(i) }
        CATransaction.commit()
    }

    private func color(_ i: Int) -> UIColor {
        let idx = i < metas.count ? metas[i].color : 0
        return Theme.padUIColors[idx % Theme.padUIColors.count]
    }

    private func style(_ i: Int) {
        let c = color(i)
        let pad = pads[i]
        let filled = hasSample[i]
        pad.backgroundColor = filled ? c.withAlphaComponent(0.16).cgColor : UIColor(white: 1, alpha: 0.03).cgColor
        pad.borderColor = (i == selected ? UIColor.white : (filled ? c.withAlphaComponent(0.35) : UIColor(white: 1, alpha: 0.08))).cgColor
        pad.borderWidth = i == selected ? 2 : 1
        fills[i].backgroundColor = c.cgColor
        names[i].string = filled ? (i < metas.count ? metas[i].name : "") : ""
        names[i].foregroundColor = UIColor(white: 1, alpha: 0.9).cgColor
        numbers[i].foregroundColor = UIColor(white: 1, alpha: filled ? 0.45 : 0.25).cgColor
        plus[i].isHidden = filled
        plus[i].foregroundColor = UIColor(white: 1, alpha: 0.18).cgColor
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let gap: CGFloat = bounds.width > 500 ? 10 : 7
        let w = (bounds.width - gap * 3) / 4
        let h = (bounds.height - gap * 3) / 4
        for i in 0..<count {
            let row = i / 4, col = i % 4
            let r = CGRect(x: CGFloat(col) * (w + gap), y: CGFloat(row) * (h + gap), width: w, height: h)
            pads[i].frame = r
            fills[i].frame = CGRect(x: 0, y: 0, width: w, height: h)
            names[i].frame = CGRect(x: 8, y: h - 20, width: w - 16, height: 15)
            numbers[i].frame = CGRect(x: w - 26, y: 6, width: 20, height: 12)
            plus[i].frame = CGRect(x: 0, y: h / 2 - 15, width: w, height: 28)
        }
        CATransaction.commit()
    }

    fileprivate func tick() {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for i in 0..<count {
            let hits = Eng.hits(i)
            if hits != lastHits[i] { lastHits[i] = hits; flash[i] = 1 }
            flash[i] *= 0.82
            let act = CGFloat(Eng.activity(i))
            let level = max(flash[i] * 0.9, act * 0.55, held[i] > 0 ? 0.35 : 0)
            fills[i].opacity = Float(hasSample[i] ? level : level * 0.3)
        }
        CATransaction.commit()
    }

    private func padIndex(at p: CGPoint) -> Int? {
        for i in 0..<count where pads[i].frame.insetBy(dx: -3, dy: -3).contains(p) { return i }
        return nil
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches {
            guard let i = padIndex(at: t.location(in: self)) else { continue }
            touchPad[ObjectIdentifier(t)] = i
            held[i] += 1
            flash[i] = 1
            onPress?(i)
        }
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { end(touches) }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { end(touches) }

    private func end(_ touches: Set<UITouch>) {
        for t in touches {
            if let i = touchPad.removeValue(forKey: ObjectIdentifier(t)) {
                held[i] = max(0, held[i] - 1)
                if held[i] == 0 { onRelease?(i) }
            }
        }
    }

}

@MainActor
private final class LinkProxy: NSObject {
    weak var owner: PadGridUIView?
    init(_ o: PadGridUIView) { owner = o }
    @objc func tick() { owner?.tick() }
}
