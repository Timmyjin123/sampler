import SwiftUI

struct StepsPanel: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var ticker: Ticker
    @State private var showAll = false
    @State private var ghost = false
    @State private var paint: (on: Bool, visited: Set<Int>)? = nil

    private let rowH: CGFloat = 24
    private let nameW: CGFloat = 64

    var body: some View {
        let _ = state.revision
        let pattern = Eng.currentPattern
        let len = Eng.patternLength(pattern)
        let pages = max(1, (len + 15) / 16)
        let page = min(state.stepPage, pages - 1)
        let rows = visibleRows(pattern: pattern, len: len)

        VStack(spacing: 8) {
            HStack(spacing: 6) {
                Segmented(items: [(16, "16"), (32, "32"), (48, "48"), (64, "64")],
                          selection: Binding(get: { len }, set: { Eng.setPatternLength(pattern, $0); state.changed() }))
                    .frame(width: 150)
                if pages > 1 {
                    Segmented(items: (0..<pages).map { ($0, "\($0 + 1)") },
                              selection: Binding(get: { page }, set: { state.stepPage = $0 }))
                        .frame(width: CGFloat(pages) * 30)
                }
                Spacer(minLength: 0)
                Chip(title: "GHOST", isOn: ghost, tint: Theme.padColor(5)) { ghost.toggle() }
                Menu {
                    Button { showAll.toggle() } label: { Label(showAll ? "Hide empty pads" : "Show all pads", systemImage: "list.bullet") }
                    Button { gp_clear_pad_steps(Int32(pattern), Int32(state.selectedPad)); state.changed() } label: {
                        Label("Clear selected pad", systemImage: "xmark")
                    }
                    Button { copyToNext(pattern) } label: { Label("Copy to next pattern", systemImage: "doc.on.doc") }
                    Divider()
                    Button(role: .destructive) { state.discardVariationPreview(); gp_clear_pattern(Int32(pattern)); state.changed() } label: {
                        Label("Clear pattern \(state.patternName(pattern))", systemImage: "trash")
                    }
                } label: {
                    Image(systemName: "ellipsis.circle").font(.system(size: 18)).foregroundColor(Theme.dim)
                }
            }

            HStack(alignment: .top, spacing: 6) {
                    VStack(spacing: 0) {
                        ForEach(rows, id: \.self) { pad in
                            Button { state.select(pad) } label: {
                                HStack(spacing: 5) {
                                    RoundedRectangle(cornerRadius: 2).fill(Theme.padColor(state.pads[pad].color)).frame(width: 4, height: 14)
                                    Text(state.pads[pad].name.isEmpty ? "Pad \(pad + 1)" : state.pads[pad].name)
                                        .font(.system(size: 10, weight: pad == state.selectedPad ? .bold : .medium, design: .rounded))
                                        .foregroundColor(pad == state.selectedPad ? Theme.text : Theme.dim)
                                        .lineLimit(1)
                                    Spacer(minLength: 0)
                                }
                                .frame(width: nameW, height: rowH)
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                        }
                    }
                    grid(rows: rows, pattern: pattern, len: len, page: page)
                        .frame(height: CGFloat(rows.count) * rowH)
            }

            variationBar(pattern: pattern)
        }
    }

    private func visibleRows(pattern: Int, len: Int) -> [Int] {
        if showAll { return Array(0..<Eng.padCount) }
        let rows = (0..<Eng.padCount).filter { pad in
            pad == state.selectedPad || Eng.hasSample(pad) || (0..<len).contains { Eng.step(pattern, pad, $0) > 0 }
        }
        return rows
    }

    private func copyToNext(_ pattern: Int) {
        let to = (pattern + 1) % Eng.patternCount
        gp_copy_pattern(Int32(pattern), Int32(to))
        state.show("Copied to pattern \(state.patternName(to))")
        state.changed()
    }

    // MARK: step grid (one Canvas — cheap to redraw at 30 fps)

    private func grid(rows: [Int], pattern: Int, len: Int, page: Int) -> some View {
        let _ = ticker.frame
        let cur = Eng.currentStep
        let first = page * 16
        let cols = min(16, len - first)
        let colors = state.pads.map { Theme.padColor($0.color) }
        let selected = state.selectedPad

        return GeometryReader { geo in
            Canvas { ctx, size in
                let cw = size.width / 16
                for (r, pad) in rows.enumerated() {
                    let y = CGFloat(r) * rowH
                    if pad == selected {
                        ctx.fill(Path(roundedRect: CGRect(x: 0, y: y + 1, width: size.width, height: rowH - 2), cornerRadius: 5),
                                 with: .color(Color.white.opacity(0.05)))
                    }
                    for c in 0..<16 {
                        let s = first + c
                        let rect = CGRect(x: CGFloat(c) * cw + 1.5, y: y + 3, width: cw - 3, height: rowH - 6)
                        if c >= cols {
                            continue
                        }
                        let v = Eng.step(pattern, pad, s)
                        let beat = (s / 4) % 2 == 0
                        let base = beat ? Color.white.opacity(0.07) : Color.white.opacity(0.035)
                        if v > 0 {
                            ctx.fill(Path(roundedRect: rect, cornerRadius: 4), with: .color(colors[pad].opacity(Double(0.35 + 0.65 * v))))
                        } else {
                            ctx.fill(Path(roundedRect: rect, cornerRadius: 4), with: .color(base))
                        }
                        if s == cur {
                            ctx.stroke(Path(roundedRect: rect.insetBy(dx: -1, dy: -1), cornerRadius: 5), with: .color(.white), lineWidth: 1.5)
                        }
                    }
                }
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        let cw = geo.size.width / 16
                        let c = Int(g.location.x / cw), r = Int(g.location.y / rowH)
                        guard c >= 0, c < cols, r >= 0, r < rows.count else { return }
                        let pad = rows[r], s = first + c
                        let key = pad * 1000 + s
                        if paint == nil {
                            let on = Eng.step(pattern, pad, s) <= 0
                            paint = (on, [])
                            state.select(pad)
                        }
                        guard var p = paint, !p.visited.contains(key) else { return }
                        p.visited.insert(key)
                        paint = p
                        Eng.setStep(pattern, pad, s, p.on ? (ghost ? 0.45 : 1.0) : 0)
                        if p.on && !Eng.isPlaying { Eng.trigger(pad, velocity: ghost ? 0.45 : 1.0) }
                    }
                    .onEnded { _ in
                        paint = nil
                        state.changed()
                    }
            )
        }
    }

    // MARK: variations

    private func variationBar(pattern: Int) -> some View {
        HStack(spacing: 6) {
            Button { state.makeVariations() } label: {
                HStack(spacing: 4) {
                    Image(systemName: "dice.fill").font(.system(size: 12, weight: .semibold))
                    Text("VARIATE").font(Theme.label)
                }
                .padding(.horizontal, 10).frame(height: 30)
                .foregroundColor(.black)
                .background(RoundedRectangle(cornerRadius: 7).fill(Theme.accent))
            }
            .buttonStyle(.plain)
            .accessibilityHint("Makes four variations of this pattern")

            if !state.variations.isEmpty {
                ForEach(0..<state.variations.count, id: \.self) { i in
                    Chip(title: "V\(i + 1)", isOn: state.activeVariation == i) { state.applyVariation(i) }
                }
                Button { state.revertVariation() } label: {
                    Image(systemName: "arrow.uturn.backward").font(.system(size: 12, weight: .semibold))
                        .frame(width: 30, height: 28)
                        .foregroundColor(state.activeVariation == nil ? Theme.accent : Theme.text)
                        .background(RoundedRectangle(cornerRadius: 7).fill(Theme.raised))
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Back to original")
                Chip(title: "KEEP", tint: Theme.go) { state.keepVariation() }
            } else {
                Text("WILD").font(Theme.label).foregroundColor(Theme.dim)
                Slider(value: Binding(get: { Double(state.variationAmount) }, set: { state.variationAmount = Float($0) }), in: 0.1...1)
                    .tint(Theme.accent)
            }
            Spacer(minLength: 0)
        }
    }
}
