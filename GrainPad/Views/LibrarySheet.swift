import SwiftUI

/// Projects, kits and export in one place.
struct LibrarySheet: View {
    @EnvironmentObject var state: AppState
    @Environment(\.dismiss) private var dismiss
    @State private var projects: [ProjectInfo] = []
    @State private var renamingID: String? = nil
    @State private var renameText = ""
    @State private var deletingID: String? = nil

    var body: some View {
        NavigationStack {
            List {
                Section("This project") {
                    Button {
                        renameText = state.projectName
                        renamingID = state.store.currentID
                    } label: {
                        HStack {
                            Text(state.projectName).foregroundColor(.primary).font(.headline)
                            Spacer()
                            Image(systemName: "pencil").foregroundColor(.secondary)
                        }
                    }
                    NavigationLink("Swap in a kit") { KitPicker(mode: .replace) }
                }

                Section("New") {
                    Button { state.newProject(kit: nil) } label: { Label("Empty project", systemImage: "square.dashed") }
                    NavigationLink { KitPicker(mode: .newProject) } label: { Label("New beat from a kit", systemImage: "square.grid.4x3.fill") }
                }

                Section("Export pattern \(state.patternName(Eng.currentPattern))") {
                    Button { state.exportPatternAudio(loops: 1) } label: { Label("Audio — 1 loop (WAV)", systemImage: "waveform") }
                    Button { state.exportPatternAudio(loops: 4) } label: { Label("Audio — 4 loops (WAV)", systemImage: "waveform.badge.plus") }
                    Button { state.exportPatternMIDI() } label: { Label("MIDI for Ableton Drum Rack", systemImage: "pianokeys") }
                    Button { state.exportSamples() } label: { Label("All pad samples (WAV)", systemImage: "square.stack.3d.up") }
                }

                Section("Projects") {
                    ForEach(projects) { p in
                        Button { state.openProject(p.id) } label: {
                            HStack {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(p.name).foregroundColor(.primary)
                                    Text(p.modified, style: .relative).font(.caption).foregroundColor(.secondary)
                                }
                                Spacer()
                                if p.id == state.store.currentID {
                                    Text("OPEN").font(.caption2.bold()).foregroundColor(Theme.accent)
                                }
                            }
                        }
                        .swipeActions(edge: .trailing) {
                            Button(role: .destructive) { deletingID = p.id } label: { Label("Delete", systemImage: "trash") }
                            Button { state.duplicateProject(p.id); reload() } label: { Label("Duplicate", systemImage: "plus.square.on.square") }
                                .tint(.blue)
                            Button { renameText = p.name; renamingID = p.id } label: { Label("Rename", systemImage: "pencil") }
                                .tint(.orange)
                        }
                    }
                }
            }
            .navigationTitle("Library")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .onAppear { state.saveNow(); reload() }
            .onChange(of: state.revision) { _ in reload() }
            .alert("Rename project", isPresented: Binding(get: { renamingID != nil }, set: { if !$0 { renamingID = nil } })) {
                TextField("Name", text: $renameText)
                Button("Save") { if let id = renamingID { state.renameProject(id, to: renameText) }; renamingID = nil; reload() }
                Button("Cancel", role: .cancel) { renamingID = nil }
            }
            .confirmationDialog("Delete this project? This can't be undone.",
                                isPresented: Binding(get: { deletingID != nil }, set: { if !$0 { deletingID = nil } }),
                                titleVisibility: .visible) {
                Button("Delete", role: .destructive) { if let id = deletingID { state.deleteProject(id) }; deletingID = nil; reload() }
                Button("Cancel", role: .cancel) { deletingID = nil }
            }
            .sheet(item: $state.share) { item in ShareSheet(items: item.urls) }
        }
        .preferredColorScheme(.dark)
    }

    private func reload() { projects = state.store.list() }
}

struct KitPicker: View {
    enum PickMode { case newProject, replace }
    let mode: PickMode
    @EnvironmentObject var state: AppState
    @State private var confirmKit: Int? = nil

    var body: some View {
        List {
            ForEach(0..<state.kitCount, id: \.self) { k in
                Button {
                    if mode == .newProject { state.newProject(kit: k) } else { confirmKit = k }
                } label: {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(state.kitName(k)).font(.headline).foregroundColor(.primary)
                        Text(state.kitDescription(k)).font(.footnote).foregroundColor(.secondary)
                    }
                    .padding(.vertical, 2)
                }
            }
        }
        .navigationTitle(mode == .newProject ? "New beat from a kit" : "Swap in a kit")
        .confirmationDialog("Replace all pads and patterns in “\(state.projectName)”?",
                            isPresented: Binding(get: { confirmKit != nil }, set: { if !$0 { confirmKit = nil } }),
                            titleVisibility: .visible) {
            Button("Replace", role: .destructive) {
                if let k = confirmKit { state.loadKitIntoCurrent(k); state.showLibrary = false }
                confirmKit = nil
            }
            Button("Cancel", role: .cancel) { confirmKit = nil }
        }
    }
}
