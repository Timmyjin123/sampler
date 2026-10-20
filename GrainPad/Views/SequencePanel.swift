import SwiftUI

/// Drum-machine sequencer: patterns, 16 big steps for the selected pad,
/// the groove library / generator and variations.
struct SequencePanel: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var ticker: Ticker
    @State private var ghost = false
    @State private var paint: (on: Bool, visited: Set<Int>)? = nil

    var body: some View {
        let _ = state.revision
        let pattern = Eng.currentPattern
        let len = Eng.patternLength(pattern)
        let pages = max(1, (len + 15) / 16)
        let page = min(state.stepPage, pages - 1)
        let pad = state.selectedPad

        VStack(spacing: 8) {
            patternRow(pattern: pattern, len: len)
            HStack(spacing: 8) {
                RoundedRectangle(cornerRadius: 2).fill(Theme.padColor(state.pads[pad].color)).frame(width: 4, height: 12)
                Text(state.pads[pad].name.isEmpty ? "Pad \(pad + 1)" : state.pads[pad].name)
                    .font(.system(size: 11, weight: .bold, design: .rounded)).foregroundColor(Theme.text).lineLimit(1)
                Text("· tap a pad to edit its steps").font(.system(size: 10, design: .rounded)).foregroundColor(Theme.faint).lineLimit(1)
                Spacer(minLength: 0)
                if pages > 1 {
                    ForEach(0..<pages, id: \.self) { p in
                        Button { state.stepPage = p } label: {
                            Text("\(p + 1)").font(.system(size: 11, weight: .bold, design: .rounded))
                                .frame(width: 22, height: 18)
                                .foregroundColor(p == page ? .black : Theme.dim)
                                .background(RoundedRectangle(cornerRadius: 5).fill(p == page ? Theme.text : Theme.raised))
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .frame(height: 18)
            steps(pattern: pattern, pad: pad, len: len, page: page)
                .frame(height: 86)
            grooveRow
            variationRow
        }
        .sheet(isPresented: $state.showGrooves) { GrooveBrowser() }
    }

    // MARK: patterns

    private func patternRow(pattern: Int, len: Int) -> some View {
        HStack(spacing: 4) {
            ForEach(0..<Eng.patternCount, id: \.self) { p in
                let current = p == pattern
                let queued = Eng.queuedPattern == p
                let empty = Eng.patternIsEmpty(p)
                Button { state.selectPattern(p) } label: {
                    Text(state.patternName(p))
                        .font(.system(size: 13, weight: .heavy, design: .rounded))
                        .frame(maxWidth: .infinity).frame(height: 28)
                        .foregroundColor(current ? .black : (empty ? Theme.faint : Theme.text))
                        .background(RoundedRectangle(cornerRadius: 7).fill(current ? Theme.accent : Theme.raised))
                        .overlay(RoundedRectangle(cornerRadius: 7).stroke(Theme.accent, lineWidth: queued ? 1.5 : 0))
                }
                .buttonStyle(.plain)
            }
            Menu {
                Picker("Length", selection: Binding(get: { len }, set: { Eng.setPatternLength(pattern, $0); state.stepPage = 0; state.changed() })) {
                    ForEach([8, 16, 24, 32, 48, 64], id: \.self) { Text("\($0) steps").tag($0) }
                }
                Divider()
                Button { copyToNext(pattern) } label: { Label("Copy to next pattern", systemImage: "doc.on.doc") }
                Button { gp_clear_pad_steps(Int32(pattern), Int32(state.selectedPad)); state.changed() } label: {
                    Label("Clear selected pad", systemImage: "xmark")
                }
                Button(role: .destructive) { state.discardVariationPreview(); gp_clear_pattern(Int32(pattern)); state.changed() } label: {
                    Label("Clear pattern \(state.patternName(pattern))", systemImage: "trash")
                }
            } label: {
                Text("\(len)")
                    .font(.system(size: 12, weight: .bold, design: .monospaced))
                    .frame(width: 38, height: 28)
                    .foregroundColor(Theme.text)
                    .background(RoundedRectangle(cornerRadius: 7).stroke(Theme.line))
            }
        }
    }

    private func copyToNext(_ pattern: Int) {
        let to = (pattern + 1) % Eng.patternCount
        gp_copy_pattern(Int32(pattern), Int32(to))
        state.show("Copied to pattern \(state.patternName(to))")
        state.changed()
    }

    // MARK: steps (2 rows × 8, drawn in one Canvas)

    private func steps(pattern: Int, pad: Int, len: Int, page: Int) -> some View {
        let _ = ticker.frame
        let cur = Eng.currentStep
        let first = page * 16
        let cols = 8, rows = 2
        let color = Theme.padColor(state.pads[pad].color)

        return GeometryReader { geo in
            let gap: CGFloat = 5
            let cw = (geo.size.width - gap * CGFloat(cols - 1)) / CGFloat(cols)
            let ch = (geo.size.height - gap) / CGFloat(rows)
            Canvas { ctx, _ in
                for i in 0..<16 {
                    let s = first + i
                    let r = CGRect(x: CGFloat(i % cols) * (cw + gap), y: CGFloat(i / cols) * (ch + gap), width: cw, height: ch)
                    guard s < len else {
                        ctx.stroke(Path(roundedRect: r.insetBy(dx: 1, dy: 1), cornerRadius: 8), with: .color(Theme.line), lineWidth: 1)
                        continue
                    }
                    let v = Eng.step(pattern, pad, s)
                    let beat = (s / 4) % 2 == 0
                    let base = beat ? Color.white.opacity(0.09) : Color.white.opacity(0.05)
                    ctx.fill(Path(roundedRect: r, cornerRadius: 8), with: .color(v > 0 ? color.opacity(Double(0.35 + 0.65 * v)) : base))
                    if s % 4 == 0 {
                        ctx.fill(Path(CGRect(x: r.minX + 6, y: r.maxY - 6, width: 8, height: 2)), with: .color(Color.white.opacity(0.35)))
                    }
                    if s == cur {
                        ctx.stroke(Path(roundedRect: r.insetBy(dx: 1, dy: 1), cornerRadius: 8), with: .color(.white), lineWidth: 2)
                    }
                }
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        let c = Int(g.location.x / (cw + gap)), r = Int(g.location.y / (ch + gap))
                        guard c >= 0, c < cols, r >= 0, r < rows else { return }
                        let s = first + r * cols + c
                        guard s < len else { return }
                        if paint == nil { paint = (Eng.step(pattern, pad, s) <= 0, []) }
                        guard var p = paint, !p.visited.contains(s) else { return }
                        p.visited.insert(s)
                        paint = p
                        let vel: Float = ghost ? 0.4 : 1.0
                        Eng.setStep(pattern, pad, s, p.on ? vel : 0)
                        if p.on && !Eng.isPlaying { Eng.trigger(pad, velocity: vel) }
                    }
                    .onEnded { _ in paint = nil; state.changed() }
            )
        }
    }

    // MARK: grooves

    private var grooveRow: some View {
        HStack(spacing: 6) {
            Button { state.rollGroove() } label: {
                HStack(spacing: 5) {
                    Image(systemName: "dice.fill").font(.system(size: 13, weight: .bold))
                    Text("NEW GROOVE").font(Theme.label)
                }
                .padding(.horizontal, 10).frame(height: 32)
                .foregroundColor(.black)
                .background(RoundedRectangle(cornerRadius: 8).fill(Theme.accent))
            }
            .buttonStyle(.plain)
            .accessibilityHint("Invents a new rhythm in the selected style")
            Menu {
                Picker("Style", selection: $state.grooveGenre) {
                    ForEach(0..<state.genreCount, id: \.self) { Text(state.genreName($0)).tag($0) }
                }
            } label: {
                HStack(spacing: 3) {
                    Text(state.genreName(state.grooveGenre)).font(Theme.label).lineLimit(1)
                    Image(systemName: "chevron.down").font(.system(size: 8, weight: .bold))
                }
                .padding(.horizontal, 9).frame(height: 32)
                .foregroundColor(Theme.text)
                .background(RoundedRectangle(cornerRadius: 8).fill(Theme.raised))
            }
            Chip(title: "LIBRARY", systemImage: "books.vertical.fill") { state.showGrooves = true }
            Spacer(minLength: 0)
            if state.canUndoGroove {
                Button { state.undoGroove() } label: {
                    Image(systemName: "arrow.uturn.backward").font(.system(size: 13, weight: .bold))
                        .frame(width: 32, height: 32)
                        .foregroundColor(Theme.text)
                        .background(RoundedRectangle(cornerRadius: 8).fill(Theme.raised))
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Undo groove")
            }
        }
    }

    // MARK: variations

    private var variationRow: some View {
        HStack(spacing: 6) {
            Button { state.makeVariations() } label: {
                HStack(spacing: 4) {
                    Image(systemName: "wand.and.stars").font(.system(size: 12, weight: .semibold))
                    Text("VARIATE").font(Theme.label)
                }
                .padding(.horizontal, 10).frame(height: 30)
                .foregroundColor(Theme.text)
                .background(RoundedRectangle(cornerRadius: 8).fill(Theme.raised))
            }
            .buttonStyle(.plain)
            if !state.variations.isEmpty {
                ForEach(0..<state.variations.count, id: \.self) { i in
                    Chip(title: "V\(i + 1)", isOn: state.activeVariation == i) { state.applyVariation(i) }
                }
                Button { state.revertVariation() } label: {
                    Image(systemName: "arrow.uturn.backward").font(.system(size: 12, weight: .semibold))
                        .frame(width: 28, height: 28)
                        .foregroundColor(state.activeVariation == nil ? Theme.accent : Theme.text)
                        .background(RoundedRectangle(cornerRadius: 7).fill(Theme.raised))
                }
                .buttonStyle(.plain)
                Chip(title: "KEEP", tint: Theme.go) { state.keepVariation() }
            } else {
                Chip(title: "GHOST", isOn: ghost, tint: Theme.padColor(5)) { ghost.toggle() }
                Text("SWING").font(Theme.label).foregroundColor(Theme.dim)
                Slider(value: Binding(get: { Double(Eng.master(GP_M_SWING)) },
                                      set: { Eng.setMaster(GP_M_SWING, Float($0)); state.revision &+= 1 }),
                       in: 0...0.5)
                    .tint(Theme.accent)
            }
            Spacer(minLength: 0)
        }
    }
}
