import SwiftUI
import UniformTypeIdentifiers

enum KnobPage: String, CaseIterable { case tone = "TONE", env = "ENVELOPE", texture = "TEXTURE", send = "SEND" }

/// Everything about the selected pad's sound, in one fixed-height panel.
struct SoundPanel: View {
    @EnvironmentObject var state: AppState
    @State private var page: KnobPage = .tone
    @State private var importing = false

    var body: some View {
        let pad = state.selectedPad
        let _ = state.revision
        let has = Eng.hasSample(pad)
        let color = Theme.padColor(state.pads[pad].color)

        VStack(spacing: 8) {
            header(pad: pad, has: has, color: color)
            if has {
                WaveformView(pad: pad).frame(height: 72)
                pageTabs(pad: pad, color: color)
                knobs(pad: pad, color: color)
                    .frame(maxWidth: .infinity)
            } else {
                emptyPad
            }
            Spacer(minLength: 0)
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audio], allowsMultipleSelection: false) { result in
            if case .success(let urls) = result, let url = urls.first { state.importFile(url) }
        }
    }

    // MARK: header

    private func header(pad: Int, has: Bool, color: Color) -> some View {
        HStack(spacing: 6) {
            Button { state.showPadSettings = true } label: {
                HStack(spacing: 6) {
                    RoundedRectangle(cornerRadius: 2).fill(color).frame(width: 4, height: 18)
                    Text(state.pads[pad].name.isEmpty ? "Pad \(pad + 1)" : state.pads[pad].name)
                        .font(.system(size: 16, weight: .bold, design: .rounded))
                        .foregroundColor(Theme.text)
                        .lineLimit(1)
                }
            }
            .buttonStyle(.plain)
            Spacer(minLength: 4)
            SampleRecordButton(source: .mic)
            Chip(title: "SOUNDS", systemImage: "music.note.list") { state.showSounds = true }
            if has {
                Menu {
                    Section("Smart — finds hits, rebuilds the loop") {
                        Button("16 slices") { state.chop(slices: 16, equal: false) }
                        Button("8 slices") { state.chop(slices: 8, equal: false) }
                        Button("4 slices") { state.chop(slices: 4, equal: false) }
                    }
                    Section("Equal") {
                        Button("16 equal") { state.chop(slices: 16, equal: true) }
                        Button("8 equal") { state.chop(slices: 8, equal: true) }
                    }
                } label: {
                    Image(systemName: "scissors").font(.system(size: 13, weight: .bold))
                        .frame(width: 34, height: 28)
                        .foregroundColor(Theme.text)
                        .background(RoundedRectangle(cornerRadius: 7).fill(Theme.raised))
                }
                .accessibilityLabel("Chop")
            }
            Menu {
                Button { importing = true } label: { Label("Import audio file", systemImage: "square.and.arrow.down") }
                Button { state.showPadSettings = true } label: { Label("Pad settings", systemImage: "slider.horizontal.3") }
                Divider()
                Button { state.copyPad() } label: { Label("Copy pad", systemImage: "doc.on.doc") }
                Button { state.pastePad() } label: { Label("Paste pad", systemImage: "doc.on.clipboard") }.disabled(!state.canPaste)
                Button { state.duplicateToNextEmpty() } label: { Label("Duplicate to empty pad", systemImage: "plus.square.on.square") }
                Divider()
                Button(role: .destructive) { state.clearPad() } label: { Label("Clear pad", systemImage: "trash") }
            } label: {
                Image(systemName: "ellipsis").font(.system(size: 14, weight: .bold))
                    .frame(width: 30, height: 28)
                    .foregroundColor(Theme.text)
                    .background(RoundedRectangle(cornerRadius: 7).fill(Theme.raised))
            }
        }
    }

    private var emptyPad: some View {
        VStack(spacing: 10) {
            Text("Empty pad").font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundColor(Theme.dim)
            HStack(spacing: 8) {
                bigAction("Pick a sound", "music.note.list") { state.showSounds = true }
                bigAction("Import file", "square.and.arrow.down") { importing = true }
            }
            Text("…or hold REC to sample the mic")
                .font(Theme.label).foregroundColor(Theme.faint)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(RoundedRectangle(cornerRadius: 12).stroke(Theme.line, style: StrokeStyle(lineWidth: 1, dash: [4, 4])))
    }

    private func bigAction(_ title: String, _ icon: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 6) {
                Image(systemName: icon).font(.system(size: 18, weight: .semibold))
                Text(title).font(Theme.label)
            }
            .frame(width: 120, height: 64)
            .foregroundColor(Theme.text)
            .background(RoundedRectangle(cornerRadius: 10).fill(Theme.raised))
        }
        .buttonStyle(.plain)
    }

    // MARK: knobs

    private func pageTabs(pad: Int, color: Color) -> some View {
        HStack(spacing: 14) {
            ForEach(KnobPage.allCases, id: \.self) { p in
                Button { page = p } label: {
                    Text(p.rawValue)
                        .font(.system(size: 10, weight: .bold, design: .rounded))
                        .tracking(0.6)
                        .foregroundColor(page == p ? color : Theme.faint)
                }
                .buttonStyle(.plain)
            }
            Spacer(minLength: 0)
            if page == .texture {
                Toggle("", isOn: state.padToggle(GP_P_MODE, pad: pad))
                    .labelsHidden()
                    .tint(color)
                    .scaleEffect(0.75)
                    .frame(height: 20)
            }
        }
        .frame(height: 20)
    }

    @ViewBuilder
    private func knobs(pad: Int, color: Color) -> some View {
        let b = { (p: GPPadParam) in state.padBinding(p, pad: pad) }
        HStack(alignment: .top, spacing: 0) {
            switch page {
            case .tone:
                Knob(label: "PITCH", value: b(GP_P_PITCH), range: -24...24, defaultValue: 0, bipolar: true, color: color, format: Fmt.semis)
                Knob(label: "LEVEL", value: b(GP_P_GAIN), range: 0...1, defaultValue: 0.8, color: color, format: Fmt.gainDb)
                Knob(label: "FILTER", value: b(GP_P_CUTOFF), range: 0...1, defaultValue: 1, color: color, format: Fmt.hz)
                Knob(label: "RESO", value: b(GP_P_RESO), range: 0...1, defaultValue: 0.1, color: color, format: Fmt.pct)
                Knob(label: "PAN", value: b(GP_P_PAN), range: -1...1, defaultValue: 0, bipolar: true, color: color, format: Fmt.pan)
            case .env:
                Knob(label: "ATTACK", value: b(GP_P_ATTACK), range: 0...2, defaultValue: 0, curve: 3, color: color, format: Fmt.secs)
                Knob(label: "DECAY", value: b(GP_P_DECAY), range: 0...4, defaultValue: 0.3, curve: 3, color: color, format: Fmt.secs)
                Knob(label: "SUSTAIN", value: b(GP_P_SUSTAIN), range: 0...1, defaultValue: 1, color: color, format: Fmt.pct)
                Knob(label: "RELEASE", value: b(GP_P_RELEASE), range: 0...4, defaultValue: 0.05, curve: 3, color: color, format: Fmt.secs)
                Knob(label: "FINE", value: b(GP_P_FINE), range: -100...100, defaultValue: 0, bipolar: true, color: color, format: Fmt.cents)
            case .texture:
                Knob(label: "SIZE", value: b(GP_P_GRAIN_SIZE), range: 0.01...0.5, defaultValue: 0.12, curve: 2, color: color, format: Fmt.secs)
                Knob(label: "DENSITY", value: b(GP_P_GRAIN_DENSITY), range: 2...80, defaultValue: 18, curve: 2, color: color, format: { "\(Int($0))/s" })
                Knob(label: "SPRAY", value: b(GP_P_GRAIN_SPRAY), range: 0...1, defaultValue: 0.15, color: color, format: Fmt.pct)
                Knob(label: "SCAN", value: b(GP_P_GRAIN_SCAN), range: 0...2, defaultValue: 0.25, curve: 2, color: color,
                     format: { $0 < 0.01 ? "freeze" : String(format: "%.2f×", $0) })
                Knob(label: "SCATTER", value: b(GP_P_GRAIN_PITCHRND), range: 0...12, defaultValue: 0, curve: 2, color: color,
                     format: { String(format: "%.1f st", $0) })
            case .send:
                Knob(label: "DELAY", value: b(GP_P_DELAY_SEND), range: 0...1, defaultValue: 0, color: color, format: Fmt.pct)
                Knob(label: "REVERB", value: b(GP_P_REVERB_SEND), range: 0...1, defaultValue: 0, color: color, format: Fmt.pct)
                Knob(label: "WIDTH", value: b(GP_P_GRAIN_STEREO), range: 0...1, defaultValue: 0.6, color: color, format: Fmt.pct)
                Spacer(minLength: 0)
            }
        }
        .opacity(page == .texture && Eng.pad(pad, GP_P_MODE) < 0.5 ? 0.4 : 1)
    }
}
