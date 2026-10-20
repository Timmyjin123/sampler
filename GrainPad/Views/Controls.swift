import SwiftUI

/// Rotary knob: drag up/down (or sideways for fine moves), double-tap resets.
struct Knob: View {
    let label: String
    @Binding var value: Float
    var range: ClosedRange<Float> = 0...1
    var defaultValue: Float = 0
    var curve: Float = 1          // >1 gives more resolution near the low end
    var bipolar = false
    var color: Color = Theme.accent
    var format: (Float) -> String = { String(format: "%.2f", $0) }

    @State private var dragStart: Float? = nil

    private var norm: Float {
        let n = (value - range.lowerBound) / (range.upperBound - range.lowerBound)
        return pow(min(max(n, 0), 1), 1 / curve)
    }
    private func set(norm n: Float) {
        let c = min(max(n, 0), 1)
        value = range.lowerBound + pow(c, curve) * (range.upperBound - range.lowerBound)
    }

    var body: some View {
        VStack(spacing: 3) {
            ZStack {
                Circle()
                    .trim(from: 0, to: 0.75)
                    .stroke(Theme.raised, style: StrokeStyle(lineWidth: 4, lineCap: .round))
                    .rotationEffect(.degrees(135))
                arc
                    .stroke(color, style: StrokeStyle(lineWidth: 4, lineCap: .round))
                    .rotationEffect(.degrees(135))
                Capsule()
                    .fill(Theme.text)
                    .frame(width: 2, height: 9)
                    .offset(y: -11)
                    .rotationEffect(.degrees(Double(-135 + 270 * norm)))
            }
            .frame(width: 40, height: 40)
            .contentShape(Rectangle().inset(by: -8))
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if dragStart == nil { dragStart = norm }
                        let n = (dragStart ?? norm) - Float(g.translation.height) / 170 + Float(g.translation.width) / 900
                        set(norm: n)
                    }
                    .onEnded { _ in dragStart = nil }
            )
            .simultaneousGesture(TapGesture(count: 2).onEnded { value = defaultValue })

            Text(label)
                .font(Theme.label)
                .foregroundColor(Theme.dim)
                .lineLimit(1)
            Text(format(value))
                .font(Theme.value)
                .foregroundColor(dragStart != nil ? color : Theme.text)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(minWidth: 52)
    }

    private var arc: some Shape {
        let pos = CGFloat(norm) * 0.75
        let mid: CGFloat = 0.375
        let from = bipolar ? min(mid, pos) : 0
        let to = bipolar ? max(mid, pos) : pos
        return Circle().trim(from: from, to: to)
    }
}

/// Small toggle / action button.
struct Chip: View {
    let title: String
    var systemImage: String? = nil
    var isOn: Bool = false
    var tint: Color = Theme.accent
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 4) {
                if let s = systemImage { Image(systemName: s).font(.system(size: 11, weight: .semibold)) }
                Text(title).font(Theme.label)
            }
            .padding(.horizontal, 10)
            .frame(height: 28)
            .foregroundColor(isOn ? .black : Theme.text)
            .background(RoundedRectangle(cornerRadius: 7).fill(isOn ? tint : Theme.raised))
        }
        .buttonStyle(.plain)
    }
}

/// Compact segmented control.
struct Segmented<T: Hashable>: View {
    let items: [(T, String)]
    @Binding var selection: T
    var tint: Color = Theme.accent

    var body: some View {
        HStack(spacing: 2) {
            ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                let on = item.0 == selection
                Button { selection = item.0 } label: {
                    Text(item.1)
                        .font(Theme.label)
                        .frame(maxWidth: .infinity)
                        .frame(height: 26)
                        .foregroundColor(on ? .black : Theme.dim)
                        .background(RoundedRectangle(cornerRadius: 6).fill(on ? tint : Color.clear))
                }
                .buttonStyle(.plain)
            }
        }
        .padding(2)
        .background(RoundedRectangle(cornerRadius: 8).fill(Theme.raised))
    }
}

/// Press-and-hold control. Reports how long it was held on release.
struct HoldButton<Content: View>: View {
    let onPress: () -> Void
    let onRelease: (TimeInterval) -> Void
    @ViewBuilder let content: (Bool) -> Content
    @State private var pressedAt: Date? = nil

    var body: some View {
        content(pressedAt != nil)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if pressedAt == nil { pressedAt = Date(); onPress() }
                    }
                    .onEnded { _ in
                        let held = Date().timeIntervalSince(pressedAt ?? Date())
                        pressedAt = nil
                        onRelease(held)
                    }
            )
    }
}

/// Thin section header.
struct SectionLabel: View {
    let text: String
    var body: some View {
        Text(text)
            .font(.system(size: 9, weight: .bold, design: .rounded))
            .tracking(1.2)
            .foregroundColor(Theme.faint)
    }
}

enum Fmt {
    static func pct(_ v: Float) -> String { "\(Int((v * 100).rounded()))%" }
    static func semis(_ v: Float) -> String {
        let r = Int(v.rounded()); return r > 0 ? "+\(r) st" : "\(r) st"
    }
    static func cents(_ v: Float) -> String { "\(Int(v.rounded()))¢" }
    static func secs(_ v: Float) -> String {
        v < 1 ? "\(Int((v * 1000).rounded())) ms" : String(format: "%.2f s", v)
    }
    static func pan(_ v: Float) -> String {
        let p = Int((v * 100).rounded())
        return p == 0 ? "C" : (p < 0 ? "L\(-p)" : "R\(p)")
    }
    static func hz(_ norm: Float) -> String {
        if norm >= 0.995 { return "open" }
        let hz = 20 * pow(1000, norm)
        return hz >= 1000 ? String(format: "%.1fk", hz / 1000) : "\(Int(hz))"
    }
    static func gainDb(_ v: Float) -> String {
        if v <= 0.0001 { return "-inf" }
        let db = 20 * log10(pow(v / 0.8, 2))
        return String(format: "%+.1f dB", db)
    }
}
