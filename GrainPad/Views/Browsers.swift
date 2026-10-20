import SwiftUI

/// All grooves by style. Tap one and it plays straight away on the current kit.
struct GrooveBrowser: View {
    @EnvironmentObject var state: AppState
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Toggle("Use the groove's tempo and swing", isOn: $state.grooveSetsTempo)
                    Text("Grooves play on whatever is on your pads: kick, snare, hats, clap and percussion are found by name, chopped pads use the first eight.")
                        .font(.footnote).foregroundColor(.secondary)
                }
                ForEach(0..<state.genreCount, id: \.self) { g in
                    Section {
                        ForEach(state.grooves(in: g), id: \.self) { i in
                            Button { state.applyGroove(i) } label: {
                                HStack {
                                    Text(state.grooveName(i)).foregroundColor(.primary)
                                    Spacer()
                                    if state.lastGroove == state.grooveName(i) {
                                        Image(systemName: "speaker.wave.2.fill").foregroundColor(Theme.accent)
                                    }
                                    Text("\(Int(gp_groove_bpm(Int32(i))))")
                                        .font(.system(.footnote, design: .monospaced)).foregroundColor(.secondary)
                                }
                            }
                        }
                        Button { state.grooveGenre = g; state.rollGroove(genre: g) } label: {
                            Label("Invent a new \(state.genreName(g)) groove", systemImage: "dice.fill")
                                .foregroundColor(Theme.accent)
                        }
                    } header: {
                        Text(state.genreName(g))
                    }
                }
            }
            .navigationTitle("Grooves")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
                if state.canUndoGroove {
                    ToolbarItem(placement: .cancellationAction) { Button("Undo") { state.undoGroove() } }
                }
            }
        }
        .preferredColorScheme(.dark)
        .presentationDetents([.medium, .large])
    }
}

/// Every kit sound, ready to drop onto the selected pad.
struct SoundBrowser: View {
    @EnvironmentObject var state: AppState
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                ForEach(0..<state.kitCount, id: \.self) { k in
                    Section {
                        ForEach(0..<16, id: \.self) { s in
                            Button { state.loadSound(kit: k, sound: s) } label: {
                                HStack(spacing: 10) {
                                    Circle().fill(Theme.padColor(AppState.kitColors[k % AppState.kitColors.count][s]))
                                        .frame(width: 10, height: 10)
                                    Text(state.kitSoundName(k, s)).foregroundColor(.primary)
                                    Spacer()
                                    Image(systemName: "arrow.down.to.line").foregroundColor(.secondary).font(.footnote)
                                }
                            }
                        }
                    } header: {
                        Text(state.kitName(k))
                    }
                }
            }
            .navigationTitle("Sounds → Pad \(state.selectedPad + 1)")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
        .preferredColorScheme(.dark)
        .presentationDetents([.medium, .large])
    }
}
