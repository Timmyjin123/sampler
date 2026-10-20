import SwiftUI
import UIKit

/// 4×4 multitouch pad grid. UIKit handles touches directly: touch-down triggers
/// immediately and each finger keeps its pad until it lifts.
struct PadGrid: UIViewRepresentable {
    let metas: [PadMeta]
    let selected: Int
    var showSteps = false
    let onPress: (Int) -> Void
    let onRelease: (Int) -> Void

    func makeUIView(context: Context) -> PadGridUIView {
        let v = PadGridUIView()
        updateUIView(v, context: context)
        return v
    }

    func updateUIView(_ v: PadGridUIView, context: Context) {
        v.onPress = onPress
        v.onRelease = onRelease
        v.showSteps = showSteps
        v.update(metas: metas, selected: selected)
    }
}

final class PadGridUIView: UIView {
    var onPress: ((Int) -> Void)?
    var onRelease: ((Int) -> Void)?
    var showSteps = false

    private let count = 16
    private var pads: [CALayer] = []
    private var fills: [CALayer] = []
    private var tags: [CALayer] = []
    private var names: [CATextLayer] = []
    private var dots: [CALayer] = []
    private var plus: [CATextLayer] = []
    private var metas: [PadMeta] = []
    private var selected = 0
    private var hasSample = [Bool](repeating: false, count: 16)
    private var touchPad: [ObjectIdentifier: Int] = [:]
    private var lastHits = [UInt32](repeating: 0, count: 16)
    private var flash = [CGFloat](repeating: 0, count: 16)
    private var held = [Int](repeating: 0, count: 16)
    private var link: CADisplayLink?
    private var frameCount = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        backgroundColor = .clear
        let scale = UIScreen.main.scale
        for _ in 0..<count {
            let pad = CALayer()
            pad.cornerRadius = 12
            pad.cornerCurve = .continuous
            pad.masksToBounds = true
            let fill = CALayer()
            pad.addSublayer(fill)
            let tag = CALayer()
            tag.cornerRadius = 2
            pad.addSublayer(tag)
            let name = CATextLayer()
            name.fontSize = 12
            name.font = UIFont.systemFont(ofSize: 12, weight: .semibold)
            name.contentsScale = scale
            name.truncationMode = .end
            name.alignmentMode = .left
            pad.addSublayer(name)
            let dot = CALayer()
            dot.cornerRadius = 3
            pad.addSublayer(dot)
            let p = CATextLayer()
            p.fontSize = 24
            p.font = UIFont.systemFont(ofSize: 24, weight: .ultraLight)
            p.contentsScale = scale
            p.alignmentMode = .center
            p.string = "+"
            pad.addSublayer(p)
            layer.addSublayer(pad)
            pads.append(pad); fills.append(fill); tags.append(tag); names.append(name); dots.append(dot); plus.append(p)
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
        let filled = hasSample[i]
        let pad = pads[i]
        pad.backgroundColor = filled ? UIColor(red: 0.13, green: 0.13, blue: 0.15, alpha: 1).cgColor
                                     : UIColor(white: 1, alpha: 0.025).cgColor
        pad.borderWidth = i == selected ? 2 : 1
        pad.borderColor = (i == selected ? UIColor(white: 1, alpha: 0.9) : UIColor(white: 1, alpha: filled ? 0.06 : 0.05)).cgColor
        fills[i].backgroundColor = c.cgColor
        tags[i].backgroundColor = c.cgColor
        tags[i].isHidden = !filled
        names[i].string = filled && i < metas.count ? metas[i].name : ""
        names[i].foregroundColor = UIColor(white: 1, alpha: 0.88).cgColor
        dots[i].backgroundColor = c.cgColor
        plus[i].isHidden = filled
        plus[i].foregroundColor = UIColor(white: 1, alpha: 0.16).cgColor
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let gap: CGFloat = bounds.width > 500 ? 10 : 8
        let w = (bounds.width - gap * 3) / 4
        let h = (bounds.height - gap * 3) / 4
        for i in 0..<count {
            let row = i / 4, col = i % 4
            pads[i].frame = CGRect(x: CGFloat(col) * (w + gap), y: CGFloat(row) * (h + gap), width: w, height: h)
            fills[i].frame = CGRect(x: 0, y: 0, width: w, height: h)
            tags[i].frame = CGRect(x: 9, y: 9, width: 16, height: 4)
            names[i].frame = CGRect(x: 9, y: h - 22, width: w - 18, height: 16)
            dots[i].frame = CGRect(x: w - 15, y: 8, width: 6, height: 6)
            plus[i].frame = CGRect(x: 0, y: h / 2 - 16, width: w, height: 30)
        }
        CATransaction.commit()
    }

    fileprivate func tick() {
        frameCount += 1
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let pattern = Eng.currentPattern
        let len = Eng.patternLength(pattern)
        for i in 0..<count {
            let hits = Eng.hits(i)
            if hits != lastHits[i] { lastHits[i] = hits; flash[i] = 1 }
            flash[i] *= 0.84
            let act = CGFloat(Eng.activity(i))
            let level = max(flash[i] * 0.85, act * 0.5, held[i] > 0 ? 0.3 : 0)
            fills[i].opacity = Float(hasSample[i] ? level : level * 0.25)
            if frameCount % 6 == 0 {
                var any = false
                if showSteps { for s in 0..<len where Eng.step(pattern, i, s) > 0 { any = true; break } }
                dots[i].isHidden = !any
            }
        }
        CATransaction.commit()
    }

    private func padIndex(at p: CGPoint) -> Int? {
        for i in 0..<count where pads[i].frame.insetBy(dx: -4, dy: -4).contains(p) { return i }
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
