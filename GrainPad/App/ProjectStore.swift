import Foundation

struct ProjectInfo: Identifiable, Equatable {
    let id: String          // folder name
    var name: String
    var modified: Date
}

/// Projects live in Documents/Projects/<id>/ (project.json + Samples/*.wav).
/// The Documents folder is visible in the Files app.
@MainActor
final class ProjectStore {
    struct PadFile: Codable {
        var meta: PadMeta
        var params: [Float]
        var sample: String?
    }
    struct PatternFile: Codable {
        var length: Int
        var hits: [[Int]]   // [pad, step, velocity 1...127]
    }
    struct ProjectFile: Codable {
        var version = 2
        var name: String?
        var master: [Float]
        var pads: [PadFile]
        var patterns: [PatternFile]
        var currentPattern: Int
    }

    private let fm = FileManager.default
    /// engine sample id → file name already written in the current project
    private var written: [Int: String] = [:]
    private(set) var currentID: String? {
        didSet { UserDefaults.standard.set(currentID, forKey: "currentProject") }
    }

    init() {
        currentID = UserDefaults.standard.string(forKey: "currentProject")
        migrateOldProject()
    }

    private var docs: URL { fm.urls(for: .documentDirectory, in: .userDomainMask)[0] }
    private var projectsDir: URL { docs.appendingPathComponent("Projects", isDirectory: true) }
    private func dir(_ id: String) -> URL { projectsDir.appendingPathComponent(id, isDirectory: true) }
    private func samplesDir(_ id: String) -> URL { dir(id).appendingPathComponent("Samples", isDirectory: true) }
    private func jsonURL(_ id: String) -> URL { dir(id).appendingPathComponent("project.json") }

    /// Version 1 kept a single project in Documents/Project.
    private func migrateOldProject() {
        let old = docs.appendingPathComponent("Project", isDirectory: true)
        guard fm.fileExists(atPath: old.appendingPathComponent("project.json").path) else { return }
        try? fm.createDirectory(at: projectsDir, withIntermediateDirectories: true)
        let id = newID()
        if (try? fm.moveItem(at: old, to: dir(id))) != nil {
            rename(id, to: "My First Project")
            if currentID == nil { currentID = id }
        }
    }

    private func newID() -> String { UUID().uuidString.prefix(8).lowercased() }

    // MARK: listing

    func list() -> [ProjectInfo] {
        guard let ids = try? fm.contentsOfDirectory(atPath: projectsDir.path) else { return [] }
        return ids.compactMap { id -> ProjectInfo? in
            let url = jsonURL(id)
            guard let data = try? Data(contentsOf: url),
                  let p = try? JSONDecoder().decode(ProjectFile.self, from: data) else { return nil }
            let date = (try? fm.attributesOfItem(atPath: url.path)[.modificationDate] as? Date) ?? Date.distantPast
            return ProjectInfo(id: id, name: p.name ?? "Untitled", modified: date)
        }
        .sorted { $0.modified > $1.modified }
    }

    func uniqueName(_ base: String) -> String {
        let names = Set(list().map(\.name))
        if !names.contains(base) { return base }
        var i = 2
        while names.contains("\(base) \(i)") { i += 1 }
        return "\(base) \(i)"
    }

    // MARK: lifecycle

    /// Starts a fresh project folder; the caller fills the engine and saves.
    func beginNew() {
        currentID = newID()
        written = [:]
    }

    func duplicate(_ id: String) -> String? {
        let nid = newID()
        do {
            try fm.copyItem(at: dir(id), to: dir(nid))
            let name = list().first { $0.id == id }?.name ?? "Project"
            rename(nid, to: uniqueName(name + " copy"))
            return nid
        } catch { return nil }
    }

    func rename(_ id: String, to name: String) {
        let url = jsonURL(id)
        guard let data = try? Data(contentsOf: url),
              var p = try? JSONDecoder().decode(ProjectFile.self, from: data) else { return }
        p.name = name
        if let out = try? JSONEncoder().encode(p) { try? out.write(to: url, options: .atomic) }
    }

    func delete(_ id: String) {
        try? fm.removeItem(at: dir(id))
        if id == currentID { currentID = nil }
    }

    // MARK: save / load

    func save(_ state: AppState) {
        if currentID == nil { beginNew() }
        guard let id = currentID else { return }
        let sdir = samplesDir(id)
        try? fm.createDirectory(at: sdir, withIntermediateDirectories: true)
        let padParamCount = Int(GP_PAD_PARAM_COUNT.rawValue)
        let masterCount = Int(GP_M_COUNT.rawValue)

        var pads: [PadFile] = []
        var used = Set<String>()
        for p in 0..<Eng.padCount {
            let params = (0..<padParamCount).map { gp_get_pad_param(Int32(p), Int32($0)) }
            var file: String? = nil
            if Eng.hasSample(p) {
                let sid = Eng.sampleID(p)
                if let existing = written[sid], fm.fileExists(atPath: sdir.appendingPathComponent(existing).path) {
                    file = existing
                } else {
                    let name = "\(UUID().uuidString.prefix(8)).wav"
                    if (try? state.audio.writePad(Int32(p), to: sdir.appendingPathComponent(name))) != nil {
                        written[sid] = name
                        file = name
                    }
                }
            }
            if let f = file { used.insert(f) }
            pads.append(PadFile(meta: state.pads[p], params: params, sample: file))
        }

        var patterns: [PatternFile] = []
        for a in 0..<Eng.patternCount {
            let steps = Eng.readSteps(a)
            var hits: [[Int]] = []
            for pad in 0..<Eng.padCount {
                for s in 0..<Eng.maxSteps {
                    let v = steps[pad * Eng.maxSteps + s]
                    if v > 0 { hits.append([pad, s, max(1, Int((v * 127).rounded()))]) }
                }
            }
            patterns.append(PatternFile(length: Eng.patternLength(a), hits: hits))
        }

        let project = ProjectFile(
            name: state.projectName,
            master: (0..<masterCount).map { gp_get_master(Int32($0)) },
            pads: pads, patterns: patterns, currentPattern: Eng.currentPattern)
        guard let data = try? JSONEncoder().encode(project) else { return }
        try? data.write(to: jsonURL(id), options: .atomic)

        if let files = try? fm.contentsOfDirectory(atPath: sdir.path) {
            for f in files where !used.contains(f) { try? fm.removeItem(at: sdir.appendingPathComponent(f)) }
        }
        written = written.filter { used.contains($0.value) }
    }

    /// Loads the last opened project. Returns false when there is none.
    func loadCurrent(into state: AppState) -> Bool {
        if let id = currentID, load(id, into: state) { return true }
        if let first = list().first, load(first.id, into: state) { return true }
        return false
    }

    func load(_ id: String, into state: AppState) -> Bool {
        guard let data = try? Data(contentsOf: jsonURL(id)),
              let project = try? JSONDecoder().decode(ProjectFile.self, from: data) else { return false }
        Eng.setPlaying(false)
        gp_all_notes_off()
        currentID = id
        written = [:]
        state.projectName = project.name ?? "Untitled"
        var loadedBy: [String: Int] = [:]   // file → first pad that loaded it
        for p in 0..<Eng.padCount {
            guard p < project.pads.count else {
                Eng.clearSample(p); gp_reset_pad_params(Int32(p))
                state.pads[p] = PadMeta(name: "", color: p / 2)
                continue
            }
            let pad = project.pads[p]
            var ok = false
            if let f = pad.sample {
                if let first = loadedBy[f] {
                    Eng.share(p, from: first); ok = true
                } else if state.audio.loadFileNow(samplesDir(id).appendingPathComponent(f), intoPad: Int32(p)) > 0 {
                    loadedBy[f] = p
                    written[Eng.sampleID(p)] = f
                    ok = true
                }
            }
            if !ok { Eng.clearSample(p) }
            gp_reset_pad_params(Int32(p))
            for (i, v) in pad.params.enumerated() where i < Int(GP_PAD_PARAM_COUNT.rawValue) {
                gp_set_pad_param(Int32(p), Int32(i), v)
            }
            state.pads[p] = pad.meta
        }
        for (i, v) in project.master.enumerated() where i < Int(GP_M_COUNT.rawValue) {
            gp_set_master(Int32(i), v)
        }
        Eng.setMaster(GP_M_REPEAT, 0)
        for a in 0..<Eng.patternCount {
            gp_clear_pattern(Int32(a))
            guard a < project.patterns.count else { Eng.setPatternLength(a, 16); continue }
            let pat = project.patterns[a]
            Eng.setPatternLength(a, pat.length)
            for h in pat.hits where h.count == 3 { Eng.setStep(a, h[0], h[1], Float(h[2]) / 127) }
        }
        Eng.selectPattern(min(max(project.currentPattern, 0), Eng.patternCount - 1))
        return true
    }
}
