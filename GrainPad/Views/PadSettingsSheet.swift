import SwiftUI

/// The less-used pad options, out of the way of the main screen.
struct PadSettingsSheet: View {
    @EnvironmentObject var state: AppState
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        let pad = state.selectedPad
        let _ = state.revision
        let srcBpm = Eng.pad(pad, GP_P_SRC_BPM)

        NavigationStack {
            Form {
                Section("Pad") {
                    TextField("Name", text: Binding(get: { state.pads[pad].name },
                                                    set: { state.pads[pad].name = String($0.prefix(18)); state.changed() }))
                    HStack(spacing: 10) {
                        ForEach(0..<Theme.padUIColors.count, id: \.self) { c in
                            Circle()
                                .fill(Theme.padColor(c))
                                .frame(width: 26, height: 26)
                                .overlay(Circle().stroke(Color.white, lineWidth: state.pads[pad].color == c ? 2.5 : 0))
                                .onTapGesture { state.pads[pad].color = c; state.changed() }
                        }
                    }
                    .padding(.vertical, 4)
                }

                Section {
                    Toggle("One-shot (plays to the end)", isOn: state.padToggle(GP_P_ONESHOT, pad: pad))
                    Toggle("Loop while held", isOn: state.padToggle(GP_P_LOOP, pad: pad))
                    Toggle("Reverse", isOn: state.padToggle(GP_P_REVERSE, pad: pad))
                    Toggle("Mono (retrigger cuts itself)", isOn: state.padToggle(GP_P_MONO, pad: pad))
                    Toggle("Texture (granular cloud)", isOn: state.padToggle(GP_P_MODE, pad: pad))
                    Picker("Choke group", selection: state.padBinding(GP_P_CHOKE, pad: pad)) {
                        Text("None").tag(Float(0))
                        Text("1").tag(Float(1))
                        Text("2").tag(Float(2))
                        Text("3").tag(Float(3))
                        Text("4").tag(Float(4))
                    }
                } header: {
                    Text("Playback")
                } footer: {
                    Text("Pads in the same choke group cut each other off, like open and closed hats.")
                }

                Section {
                    HStack {
                        Text("Sample tempo")
                        Spacer()
                        Text(srcBpm > 0 ? String(format: "%.1f BPM", srcBpm) : "unknown").foregroundColor(.secondary)
                    }
                    Button("Detect tempo") { state.detectTempo() }
                    Picker("Follow project tempo", selection: state.padBinding(GP_P_SYNC, pad: pad)) {
                        Text("Off").tag(Float(0))
                        Text("Repitch").tag(Float(1))
                        Text("Stretch").tag(Float(2))
                    }
                    .disabled(srcBpm <= 0)
                    if srcBpm > 0 {
                        Button(String(format: "Set project tempo to %.1f", srcBpm)) { state.matchProjectTempo() }
                    }
                } header: {
                    Text("Tempo")
                } footer: {
                    Text("Repitch speeds the sample up or down like a turntable; Stretch keeps the pitch.")
                }

                Section {
                    Button("Copy pad") { state.copyPad() }
                    Button("Paste pad") { state.pastePad() }.disabled(!state.canPaste)
                    Button("Duplicate to an empty pad") { state.duplicateToNextEmpty() }
                    Button("Reset settings") { gp_reset_pad_params(Int32(pad)); state.changed() }
                    Button("Clear pad", role: .destructive) { state.clearPad(); dismiss() }
                }
            }
            .navigationTitle(state.pads[pad].name.isEmpty ? "Pad \(pad + 1)" : state.pads[pad].name)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
        .preferredColorScheme(.dark)
        .presentationDetents([.medium, .large])
    }
}
