import SwiftUI
import UniformTypeIdentifiers

enum KnobPage: String, CaseIterable { case main = "MAIN", env = "ENV", texture = "TEXTURE", send = "SEND" }

struct SamplePanel: View {
    @EnvironmentObject var state: AppState
    @State private var importing = false
    @State private var page: KnobPage = .main
    @State private var renaming = false
    @State private var newName = ""

    var body: some View {
        let pad = state.selectedPad
        let _ = state.revision
        let has = Eng.hasSample(pad)
        let color = Theme.padColor(state.pads[pad].color)

        VStack(spacing: 8) {
            header(pad: pad, color: color)
            WaveformView(pad: pad)
                .frame(height: 84)
            actionRow(pad: pad, has: has)
            if has {
                playbackRow(pad: pad)
                Segmented(items: KnobPage.allCases.map { ($0, $0.rawValue) }, selection: $page, tint: color)
                knobs(pad: pad, color: color)
                    .frame(maxWidth: .infinity)
            }
            Spacer(minLength: 0)
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audio], allowsMultipleSelection: false) { result in
            if case .success(let urls) = result, let url = urls.first { state.importFile(url) }
        }
        .alert("Pad name", isPresented: $renaming) {
            TextField("Name", text: $newName)
            Button("OK") { state.pads[pad].name = String(newName.prefix(18)); state.changed() }
            Button("Cancel", role: .cancel) {}
        }
        .onChange(of: Eng.pad(pad, GP_P_MODE) > 0.5) { isTexture in
            if isTexture { page = .texture } else if page == .texture { page = .main }
        }
    }

    // MARK: header

    private func header(pad: Int, color: Color) -> some View {
        HStack(spacing: 8) {
            Circle().fill(color).frame(width: 10, height: 10)
            Button {
                newName = state.pads[pad].name
                renaming = true
            } label: {
                Text(state.pads[pad].name.isEmpty ? "Pad \(pad + 1)" : state.pads[pad].name)
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                    .foregroundColor(Theme.text)
                    .lineLimit(1)
            }
            .buttonStyle(.plain)
            if Eng.hasSample(pad) {
                Text(String(format: "%.2fs", Eng.duration(pad)))
                    .font(Theme.value).foregroundColor(Theme.dim)
            }
            Spacer()
            HStack(spacing: 5) {
                ForEach(0..<Theme.padUIColors.count, id: \.self) { c in
                    Circle()
                        .fill(Theme.padColor(c))
                        .frame(width: 12, height: 12)
                        .overlay(Circle().stroke(Color.white, lineWidth: state.pads[pad].color == c ? 1.5 : 0))
                        .onTapGesture { state.pads[pad].color = c; state.changed() }
                }
            }
            Menu {
                Button { state.copyPad() } label: { Label("Copy pad", systemImage: "doc.on.doc") }
                Button { state.pastePad() } label: { Label("Paste pad", systemImage: "doc.on.clipboard") }
                    .disabled(!state.canPaste)
                Button { state.duplicateToNextEmpty() } label: { Label("Duplicate to empty pad", systemImage: "plus.square.on.square") }
                Button { gp_reset_pad_params(Int32(pad)); state.changed() } label: { Label("Reset settings", systemImage: "arrow.counterclockwise") }
                Divider()
                Button(role: .destructive) { state.clearPad() } label: { Label("Clear pad", systemImage: "trash") }
            } label: {
                Image(systemName: "ellipsis.circle").font(.system(size: 18)).foregroundColor(Theme.dim)
            }
        }
    }

    // MARK: actions

    private func actionRow(pad: Int, has: Bool) -> some View {
        HStack(spacing: 6) {
            SampleRecordButton(source: .mic)
            SampleRecordButton(source: .resample)
            Chip(title: "IMPORT", systemImage: "square.and.arrow.down") { importing = true }
            Spacer(minLength: 0)
            if has {
                Menu {
                    Section("Smart — finds the hits, rebuilds the loop") {
                        Button("16 slices") { state.chop(slices: 16, equal: false) }
                        Button("8 slices") { state.chop(slices: 8, equal: false) }
                        Button("4 slices") { state.chop(slices: 4, equal: false) }
                    }
                    Section("Equal") {
                        Button("16 equal") { state.chop(slices: 16, equal: true) }
                        Button("8 equal") { state.chop(slices: 8, equal: true) }
                        Button("4 equal") { state.chop(slices: 4, equal: true) }
                    }
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "scissors").font(.system(size: 11, weight: .semibold))
                        Text("CHOP").font(Theme.label)
                    }
                    .padding(.horizontal, 10).frame(height: 28)
                    .foregroundColor(.black)
                    .background(RoundedRectangle(cornerRadius: 7).fill(Theme.accent))
                }
            }
        }
    }

    // MARK: playback toggles

    private func playbackRow(pad: Int) -> some View {
        let srcBpm = Eng.pad(pad, GP_P_SRC_BPM)
        return VStack(spacing: 6) {
            HStack(spacing: 6) {
                Chip(title: "1-SHOT", isOn: Eng.pad(pad, GP_P_ONESHOT) > 0.5) { flip(GP_P_ONESHOT) }
                Chip(title: "LOOP", isOn: Eng.pad(pad, GP_P_LOOP) > 0.5) { flip(GP_P_LOOP) }
                Chip(title: "REV", isOn: Eng.pad(pad, GP_P_REVERSE) > 0.5) { flip(GP_P_REVERSE) }
                Chip(title: "MONO", isOn: Eng.pad(pad, GP_P_MONO) > 0.5) { flip(GP_P_MONO) }
                Chip(title: "TEXTURE", systemImage: "sparkles", isOn: Eng.pad(pad, GP_P_MODE) > 0.5, tint: Theme.padColor(6)) {
                    flip(GP_P_MODE)
                }
                Spacer(minLength: 0)
            }
            HStack(spacing: 6) {
                Button { state.detectTempo() } label: {
                    Text(srcBpm > 0 ? String(format: "%.1f BPM", srcBpm) : "DETECT BPM")
                        .font(Theme.label).monospacedDigit()
                        .padding(.horizontal, 8).frame(height: 26)
                        .foregroundColor(Theme.text)
                        .background(RoundedRectangle(cornerRadius: 6).stroke(Theme.line))
                }
                .buttonStyle(.plain)
                Segmented(items: [(Float(0), "OFF"), (Float(1), "REPITCH"), (Float(2), "STRETCH")],
                          selection: Binding(get: { Eng.pad(pad, GP_P_SYNC) },
                                             set: { Eng.setPad(pad, GP_P_SYNC, $0); state.changed() }))
                    .disabled(srcBpm <= 0)
                    .opacity(srcBpm > 0 ? 1 : 0.4)
                if srcBpm > 0 && abs(srcBpm - Eng.master(GP_M_BPM)) > 0.05 {
                    Button { state.matchProjectTempo() } label: {
                        Image(systemName: "metronome").font(.system(size: 13))
                            .frame(width: 30, height: 26)
                            .foregroundColor(Theme.accent)
                            .background(RoundedRectangle(cornerRadius: 6).fill(Theme.raised))
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Set project tempo to sample tempo")
                }
            }
        }
    }

    private func flip(_ p: GPPadParam) {
        let pad = state.selectedPad
        Eng.setPad(pad, p, Eng.pad(pad, p) > 0.5 ? 0 : 1)
        state.changed()
    }

    // MARK: knobs

    @ViewBuilder
    private func knobs(pad: Int, color: Color) -> some View {
        let b = { (p: GPPadParam) in state.padBinding(p, pad: pad) }
        switch page {
        case .main:
            HStack(alignment: .top, spacing: 4) {
                Knob(label: "PITCH", value: b(GP_P_PITCH), range: -24...24, defaultValue: 0, bipolar: true, color: color, format: Fmt.semis)
                Knob(label: "FINE", value: b(GP_P_FINE), range: -100...100, defaultValue: 0, bipolar: true, color: color, format: Fmt.cents)
                Knob(label: "LEVEL", value: b(GP_P_GAIN), range: 0...1, defaultValue: 0.8, color: color, format: Fmt.gainDb)
                Knob(label: "PAN", value: b(GP_P_PAN), range: -1...1, defaultValue: 0, bipolar: true, color: color, format: Fmt.pan)
                Knob(label: "FILTER", value: b(GP_P_CUTOFF), range: 0...1, defaultValue: 1, color: color, format: Fmt.hz)
                Knob(label: "RESO", value: b(GP_P_RESO), range: 0...1, defaultValue: 0.1, color: color, format: Fmt.pct)
            }
        case .env:
            HStack(alignment: .top, spacing: 4) {
                Knob(label: "ATTACK", value: b(GP_P_ATTACK), range: 0...2, defaultValue: 0, curve: 3, color: color, format: Fmt.secs)
                Knob(label: "DECAY", value: b(GP_P_DECAY), range: 0...4, defaultValue: 0.3, curve: 3, color: color, format: Fmt.secs)
                Knob(label: "SUSTAIN", value: b(GP_P_SUSTAIN), range: 0...1, defaultValue: 1, color: color, format: Fmt.pct)
                Knob(label: "RELEASE", value: b(GP_P_RELEASE), range: 0...4, defaultValue: 0.05, curve: 3, color: color, format: Fmt.secs)
                VStack(spacing: 4) {
                    SectionLabel(text: "CHOKE")
                    Segmented(items: [(Float(0), "–"), (Float(1), "1"), (Float(2), "2"), (Float(3), "3")],
                              selection: b(GP_P_CHOKE), tint: color)
                        .frame(width: 112)
                }
            }
        case .texture:
            VStack(spacing: 6) {
                if Eng.pad(pad, GP_P_MODE) < 0.5 {
                    Text("Turn on TEXTURE to play this pad as a granular cloud")
                        .font(Theme.label).foregroundColor(Theme.dim)
                }
                HStack(alignment: .top, spacing: 4) {
                    Knob(label: "SIZE", value: b(GP_P_GRAIN_SIZE), range: 0.01...0.5, defaultValue: 0.12, curve: 2, color: color, format: Fmt.secs)
                    Knob(label: "DENSITY", value: b(GP_P_GRAIN_DENSITY), range: 2...80, defaultValue: 18, curve: 2, color: color, format: { "\(Int($0))/s" })
                    Knob(label: "SPRAY", value: b(GP_P_GRAIN_SPRAY), range: 0...1, defaultValue: 0.15, color: color, format: Fmt.pct)
                    Knob(label: "SCAN", value: b(GP_P_GRAIN_SCAN), range: 0...2, defaultValue: 0.25, curve: 2, color: color, format: { $0 < 0.01 ? "freeze" : String(format: "%.2f×", $0) })
                    Knob(label: "SCATTER", value: b(GP_P_GRAIN_PITCHRND), range: 0...12, defaultValue: 0, curve: 2, color: color, format: { String(format: "%.1f st", $0) })
                    Knob(label: "WIDTH", value: b(GP_P_GRAIN_STEREO), range: 0...1, defaultValue: 0.6, color: color, format: Fmt.pct)
                }
                .opacity(Eng.pad(pad, GP_P_MODE) > 0.5 ? 1 : 0.45)
            }
        case .send:
            HStack(alignment: .top, spacing: 4) {
                Knob(label: "DELAY", value: b(GP_P_DELAY_SEND), range: 0...1, defaultValue: 0, color: color, format: Fmt.pct)
                Knob(label: "REVERB", value: b(GP_P_REVERB_SEND), range: 0...1, defaultValue: 0, color: color, format: Fmt.pct)
                Spacer()
            }
        }
    }
}
