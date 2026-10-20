import Foundation

/// Saves the current project to Documents/Project (JSON + one WAV per distinct sample).
/// The Documents folder is visible in the Files app, so samples can be taken out too.
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
        var version = 1
        var master: [Float]
        var pads: [PadFile]
        var patterns: [PatternFile]
        var currentPattern: Int
    }

    /// engine sample id → file name already on disk
    private var written: [Int: String] = [:]

    private var root: URL {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return docs.appendingPathComponent("Project", isDirectory: true)
    }
    private var samplesDir: URL { root.appendingPathComponent("Samples", isDirectory: true) }
    private var jsonURL: URL { root.appendingPathComponent("project.json") }

    func resetFiles() { written = [:] }

    func save(_ state: AppState) {
        let fm = FileManager.default
        try? fm.createDirectory(at: samplesDir, withIntermediateDirectories: true)
        let padParamCount = Int(GP_PAD_PARAM_COUNT.rawValue)
        let masterCount = Int(GP_M_COUNT.rawValue)

        var pads: [PadFile] = []
        var used = Set<String>()
        for p in 0..<Eng.padCount {
            let params = (0..<padParamCount).map { gp_get_pad_param(Int32(p), Int32($0)) }
            var file: String? = nil
            if Eng.hasSample(p) {
                let id = Eng.sampleID(p)
                if let existing = written[id], fm.fileExists(atPath: samplesDir.appendingPathComponent(existing).path) {
                    file = existing
                } else {
                    let name = "\(UUID().uuidString.prefix(8)).wav"
                    let url = samplesDir.appendingPathComponent(name)
                    do {
                        try state.audio.writePad(Int32(p), to: url)
                        written[id] = name
                        file = name
                    } catch {
                        file = nil
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
            master: (0..<masterCount).map { gp_get_master(Int32($0)) },
            pads: pads, patterns: patterns, currentPattern: Eng.currentPattern)
        do {
            let data = try JSONEncoder().encode(project)
            try data.write(to: jsonURL, options: .atomic)
        } catch {
            return
        }
        // remove samples no pad uses any more
        if let files = try? fm.contentsOfDirectory(atPath: samplesDir.path) {
            for f in files where !used.contains(f) {
                try? fm.removeItem(at: samplesDir.appendingPathComponent(f))
            }
        }
        written = written.filter { used.contains($0.value) }
    }

    /// Returns false when there is no saved project.
    func load(into state: AppState) -> Bool {
        guard let data = try? Data(contentsOf: jsonURL),
              let project = try? JSONDecoder().decode(ProjectFile.self, from: data) else { return false }
        written = [:]
        var loadedBy: [String: Int] = [:]   // file → first pad that loaded it
        for (p, pad) in project.pads.enumerated() where p < Eng.padCount {
            if let f = pad.sample {
                if let first = loadedBy[f] {
                    Eng.share(p, from: first)
                } else if state.audio.loadFileNow(samplesDir.appendingPathComponent(f), intoPad: Int32(p)) > 0 {
                    loadedBy[f] = p
                    written[Eng.sampleID(p)] = f
                }
            } else {
                Eng.clearSample(p)
            }
            for (i, v) in pad.params.enumerated() where i < Int(GP_PAD_PARAM_COUNT.rawValue) {
                gp_set_pad_param(Int32(p), Int32(i), v)
            }
            state.pads[p] = pad.meta
        }
        for (i, v) in project.master.enumerated() where i < Int(GP_M_COUNT.rawValue) {
            gp_set_master(Int32(i), v)
        }
        Eng.setMaster(GP_M_REPEAT, 0)
        for (a, pat) in project.patterns.enumerated() where a < Eng.patternCount {
            gp_clear_pattern(Int32(a))
            Eng.setPatternLength(a, pat.length)
            for h in pat.hits where h.count == 3 {
                Eng.setStep(a, h[0], h[1], Float(h[2]) / 127)
            }
        }
        Eng.selectPattern(min(max(project.currentPattern, 0), Eng.patternCount - 1))
        return true
    }
}
