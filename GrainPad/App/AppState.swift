import SwiftUI
import Combine

/// What the panel above the pads shows.
enum Mode: String, CaseIterable, Identifiable {
    case sound = "SOUND", sequence = "SEQUENCE", fx = "FX"
    var id: String { rawValue }
}

struct PadMeta: Codable, Equatable {
    var name: String
    var color: Int
}

struct FXPreset: Identifiable {
    let id = UUID()
    let name: String
    let values: [(GPMasterParam, Float)]
}

@MainActor
final class AppState: ObservableObject {
    @Published var selectedPad = 0
    @Published var mode: Mode = .sound
    @Published var pads: [PadMeta]
    @Published var projectName = "Untitled"
    /// Bumped whenever engine-side state changes, so views that read the engine re-render.
    @Published var revision = 0
    @Published var recordingSource: GPRecordSource? = nil
    @Published var recordLatched = false
    @Published var toast: String? = nil
    @Published var stepPage = 0

    // sheets
    @Published var showLibrary = false
    @Published var showPadSettings = false
    @Published var showSounds = false
    @Published var showGrooves = false
    @Published var share: ShareItems? = nil

    // variations
    @Published var variations: [[Float]] = []
    @Published var variationOriginal: [Float]? = nil
    @Published var activeVariation: Int? = nil
    @Published var variationAmount: Float = 0.5

    let audio = GPAudio.shared
    let store = ProjectStore()
    private var clipboard: (params: [Float], pad: Int, meta: PadMeta)?
    private var dirty = false
    private var timers: [Timer] = []
    private var takeCounter = 1
    private var toastToken = 0
    private var pendingStart = false
    private var booted = false

    static let kitColors: [[Int]] = [
        [0, 1, 2, 2, 7, 3, 0, 3, 4, 4, 5, 5, 6, 7, 6, 6],
        [0, 1, 7, 2, 2, 3, 7, 3, 4, 4, 4, 4, 5, 6, 6, 0],
        [0, 7, 2, 2, 2, 3, 1, 1, 3, 4, 4, 5, 5, 6, 6, 2],
        [0, 0, 7, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 6, 0, 6],
        [0, 1, 2, 2, 3, 3, 7, 6, 5, 5, 5, 5, 4, 4, 1, 6],
        [0, 1, 7, 2, 2, 1, 3, 3, 3, 6, 4, 4, 5, 6, 7, 2],
        [0, 2, 3, 1, 6, 6, 4, 7, 4, 5, 0, 1, 1, 3, 6, 5],
    ]

    static let fxPresets: [FXPreset] = [
        FXPreset(name: "Clean", values: [(GP_M_FILTER, 0.5), (GP_M_DRIVE, 0), (GP_M_CRUSH, 0), (GP_M_DELAY_FB, 0.3), (GP_M_REVERB_SIZE, 0.45)]),
        FXPreset(name: "Dusty", values: [(GP_M_FILTER, 0.38), (GP_M_DRIVE, 0.12), (GP_M_CRUSH, 0.15), (GP_M_REVERB_SIZE, 0.5), (GP_M_REVERB_DAMP, 0.7)]),
        FXPreset(name: "Tape", values: [(GP_M_FILTER, 0.44), (GP_M_DRIVE, 0.3), (GP_M_CRUSH, 0.03), (GP_M_DELAY_TIME, 5), (GP_M_DELAY_TONE, 0.3)]),
        FXPreset(name: "Club", values: [(GP_M_FILTER, 0.5), (GP_M_DRIVE, 0.22), (GP_M_CRUSH, 0), (GP_M_DELAY_TIME, 3), (GP_M_DELAY_FB, 0.3), (GP_M_REVERB_SIZE, 0.35)]),
        FXPreset(name: "Dub", values: [(GP_M_FILTER, 0.47), (GP_M_DRIVE, 0.1), (GP_M_DELAY_TIME, 4), (GP_M_DELAY_FB, 0.7), (GP_M_DELAY_TONE, 0.35), (GP_M_REVERB_SIZE, 0.7)]),
        FXPreset(name: "Space", values: [(GP_M_FILTER, 0.5), (GP_M_DRIVE, 0), (GP_M_DELAY_TIME, 6), (GP_M_DELAY_FB, 0.55), (GP_M_REVERB_SIZE, 0.95), (GP_M_REVERB_DAMP, 0.3)]),
        FXPreset(name: "Crushed", values: [(GP_M_FILTER, 0.5), (GP_M_DRIVE, 0.45), (GP_M_CRUSH, 0.55), (GP_M_REVERB_SIZE, 0.3)]),
        FXPreset(name: "Radio", values: [(GP_M_FILTER, 0.66), (GP_M_RESO, 0.5), (GP_M_DRIVE, 0.35), (GP_M_CRUSH, 0.2)]),
    ]

    init() {
        pads = (0..<Eng.padCount).map { PadMeta(name: "", color: $0 / 2) }
    }

    // MARK: lifecycle

    func boot() {
        guard !booted else { return }
        booted = true
        do { try audio.start() } catch { show("Audio failed to start: \(error.localizedDescription)") }
        if !store.loadCurrent(into: self) {
            store.beginNew()
            applyKit(0)
            projectName = "First Beat"
            saveNow()
        }
        changed()
        timers.append(Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { _ in
            Task { @MainActor in gp_collect_garbage() }
        })
        timers.append(Timer.scheduledTimer(withTimeInterval: 8, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.autosave() }
        })
    }

    func autosave() {
        if Eng.isRecording { dirty = true }
        guard dirty else { return }
        dirty = false
        store.save(self)
    }

    func saveNow() { dirty = false; store.save(self) }

    func changed() { revision &+= 1; dirty = true }

    // MARK: bindings

    func padBinding(_ p: GPPadParam, pad: Int? = nil) -> Binding<Float> {
        let pad = pad ?? selectedPad
        return Binding(
            get: { Eng.pad(pad, p) },
            set: { [weak self] v in Eng.setPad(pad, p, v); self?.changed() })
    }
    func padToggle(_ p: GPPadParam, pad: Int? = nil) -> Binding<Bool> {
        let pad = pad ?? selectedPad
        return Binding(
            get: { Eng.pad(pad, p) > 0.5 },
            set: { [weak self] v in Eng.setPad(pad, p, v ? 1 : 0); self?.changed() })
    }
    func masterBinding(_ p: GPMasterParam) -> Binding<Float> {
        Binding(
            get: { Eng.master(p) },
            set: { [weak self] v in Eng.setMaster(p, v); self?.changed() })
    }

    func select(_ pad: Int) {
        if selectedPad != pad { selectedPad = pad }
    }

    // MARK: transport

    func togglePlay() {
        Eng.setPlaying(!Eng.isPlaying)
        changed()
    }
    func toggleRecordSequence() {
        let on = !Eng.isRecording
        Eng.setRecording(on)
        if on && !Eng.isPlaying { Eng.setPlaying(true) }
        changed()
    }
    func selectPattern(_ p: Int) {
        discardVariationPreview()
        Eng.selectPattern(p)
        stepPage = 0
        changed()
    }

    // MARK: sampling

    func beginRecord(_ source: GPRecordSource) {
        guard recordingSource == nil else { return }
        pendingStart = true
        if source == .mic {
            audio.requestMicPermission { [weak self] granted in
                guard let self else { return }
                if granted { self.startCapture(.mic) }
                else {
                    self.pendingStart = false
                    self.recordLatched = false
                    self.show("Microphone access is off — enable it in Settings → GrainPad")
                }
            }
        } else {
            startCapture(source)
        }
    }

    private func startCapture(_ source: GPRecordSource) {
        guard pendingStart, recordingSource == nil else { return }
        pendingStart = false
        if audio.beginRecording(source) {
            recordingSource = source
        } else {
            recordLatched = false
            show("Couldn't start recording")
        }
    }

    func endRecord() {
        guard let src = recordingSource else {
            pendingStart = false
            recordLatched = false
            return
        }
        let pad = selectedPad
        recordingSource = nil
        recordLatched = false
        audio.finishRecording(intoPad: Int32(pad)) { [weak self] frames in
            guard let self else { return }
            if frames <= 0 { self.show("Nothing recorded — too quiet"); return }
            gp_reset_pad_params(Int32(pad))
            let name = src == .mic ? "Take \(self.takeCounter)" : "Bounce \(self.takeCounter)"
            self.takeCounter += 1
            self.pads[pad].name = name
            self.afterNewSample(pad)
            self.show(String(format: "%@ · %.1f s", name, Eng.duration(pad)))
        }
    }

    func importFile(_ url: URL) {
        let pad = selectedPad
        let name = url.deletingPathExtension().lastPathComponent
        audio.loadFile(url, intoPad: Int32(pad), autoTrim: false, normalize: false) { [weak self] frames in
            guard let self else { return }
            if frames <= 0 { self.show("Couldn't read that file"); return }
            gp_reset_pad_params(Int32(pad))
            self.pads[pad].name = String(name.prefix(18))
            self.afterNewSample(pad)
            self.show(String(format: "Loaded %.1f s", Eng.duration(pad)))
        }
    }

    private func afterNewSample(_ pad: Int) {
        if Eng.duration(pad) > 1.2 {
            let bpm = Eng.detectBPM(pad)
            if bpm > 0 { Eng.setPad(pad, GP_P_SRC_BPM, bpm) }
        }
        changed()
        saveNow()
    }

    // MARK: smart tools

    func detectTempo() {
        let bpm = Eng.detectBPM(selectedPad)
        if bpm > 0 {
            Eng.setPad(selectedPad, GP_P_SRC_BPM, bpm)
            show(String(format: "Tempo %.1f BPM", bpm))
        } else {
            show("No clear tempo in this sample")
        }
        changed()
    }

    func matchProjectTempo() {
        let bpm = Eng.pad(selectedPad, GP_P_SRC_BPM)
        guard bpm > 0 else { return }
        Eng.setMaster(GP_M_BPM, bpm)
        show(String(format: "Project tempo → %.1f", bpm))
        changed()
    }

    /// Chops the selected pad onto pads starting at it; smart chops also write
    /// a pattern that replays the loop.
    func chop(slices: Int, equal: Bool) {
        let src = selectedPad
        guard Eng.hasSample(src) else { return }
        var target = Eng.currentPattern
        if !Eng.patternIsEmpty(target) {
            target = (0..<Eng.patternCount).first { Eng.patternIsEmpty($0) } ?? target
        }
        let baseName = pads[src].name.isEmpty ? "Slice" : String(pads[src].name.prefix(8))
        let color = pads[src].color
        let n = Int(gp_smart_chop(Int32(src), Int32(src), Int32(slices), equal ? 1 : 0, equal ? -1 : Int32(target)))
        guard n > 0 else { show("Couldn't find slices"); return }
        for i in 0..<n {
            let p = (src + i) % Eng.padCount
            pads[p] = PadMeta(name: "\(baseName) \(i + 1)", color: color)
        }
        var msg = "\(n) slices"
        if !equal && Eng.pad(src, GP_P_SRC_BPM) > 0 {
            if target != Eng.currentPattern { Eng.selectPattern(target) }
            msg += " · pattern \(patternName(target)) plays the loop"
        }
        show(msg)
        changed()
        saveNow()
    }

    func makeVariations() {
        let pattern = Eng.currentPattern
        if variationOriginal == nil { variationOriginal = Eng.readSteps(pattern) }
        let current = Eng.readSteps(pattern)
        if let orig = variationOriginal { Eng.applySteps(pattern, orig) }
        let base = UInt32.random(in: 1...UInt32.max - 8)
        variations = (0..<4).map { i -> [Float] in
            var out = [Float](repeating: 0, count: Eng.padCount * Eng.maxSteps)
            let amount = min(1, max(0.05, variationAmount * (0.75 + 0.2 * Float(i))))
            out.withUnsafeMutableBufferPointer { gp_make_variation(Int32(pattern), base + UInt32(i), amount, $0.baseAddress) }
            return out
        }
        Eng.applySteps(pattern, current)
        if let a = activeVariation { Eng.applySteps(pattern, variations[a]) }
        changed()
    }

    func applyVariation(_ i: Int) {
        guard i < variations.count else { return }
        Eng.applySteps(Eng.currentPattern, variations[i])
        activeVariation = i
        changed()
    }

    func revertVariation() {
        if let o = variationOriginal { Eng.applySteps(Eng.currentPattern, o) }
        activeVariation = nil
        changed()
    }

    func keepVariation() {
        variations = []; variationOriginal = nil; activeVariation = nil
        changed()
    }

    func discardVariationPreview() {
        if variationOriginal != nil { revertVariation(); keepVariation() }
    }

    // MARK: pad operations

    func clearPad() {
        Eng.clearSample(selectedPad)
        gp_reset_pad_params(Int32(selectedPad))
        gp_clear_pad_steps(Int32(Eng.currentPattern), Int32(selectedPad))
        pads[selectedPad].name = ""
        changed()
    }

    func copyPad() {
        let p = selectedPad
        let params = (0..<Int(GP_PAD_PARAM_COUNT.rawValue)).map { gp_get_pad_param(Int32(p), Int32($0)) }
        clipboard = (params, p, pads[p])
        show("Copied \(pads[p].name.isEmpty ? "pad" : pads[p].name)")
    }

    var canPaste: Bool { clipboard != nil }

    func pastePad() {
        guard let c = clipboard, c.pad != selectedPad else { return }
        let p = selectedPad
        if Eng.hasSample(c.pad) { Eng.share(p, from: c.pad) }
        for (i, v) in c.params.enumerated() { gp_set_pad_param(Int32(p), Int32(i), v) }
        pads[p] = c.meta
        changed()
    }

    func duplicateToNextEmpty() {
        guard let empty = (1..<Eng.padCount).map({ (selectedPad + $0) % Eng.padCount }).first(where: { !Eng.hasSample($0) })
        else { show("No empty pads"); return }
        copyPad()
        let keep = selectedPad
        selectedPad = empty
        pastePad()
        pads[empty].name = pads[keep].name + "'"
        changed()
    }

    // MARK: kits and sounds

    var kitCount: Int { Int(gp_kit_count()) }
    func kitName(_ k: Int) -> String { String(cString: gp_kit_name(Int32(k))) }
    func kitDescription(_ k: Int) -> String { String(cString: gp_kit_description(Int32(k))) }
    func kitSoundName(_ k: Int, _ pad: Int) -> String { String(cString: gp_kit_pad_name(Int32(k), Int32(pad))) }

    private func applyKit(_ k: Int) {
        gp_load_kit(Int32(k))
        let colors = Self.kitColors[k % Self.kitColors.count]
        for p in 0..<Eng.padCount { pads[p] = PadMeta(name: kitSoundName(k, p), color: colors[p]) }
        keepVariation()
        selectedPad = 0
    }

    /// Replaces the sounds and patterns of the current project with a kit.
    func loadKitIntoCurrent(_ k: Int) {
        applyKit(k)
        changed()
        saveNow()
        show("\(kitName(k)) loaded — press play")
    }

    /// Loads one sound onto the selected pad.
    func loadSound(kit: Int, sound: Int) {
        let p = selectedPad
        gp_load_kit_sound(Int32(kit), Int32(sound), Int32(p))
        pads[p] = PadMeta(name: kitSoundName(kit, sound), color: Self.kitColors[kit % Self.kitColors.count][sound])
        Eng.trigger(p)
        changed()
    }

    func applyFX(_ preset: FXPreset) {
        for (param, value) in preset.values { Eng.setMaster(param, value) }
        changed()
        show("FX: \(preset.name)")
    }

    // MARK: grooves

    @Published var grooveGenre = 0
    @Published var grooveSetsTempo = true
    @Published var lastGroove: String? = nil
    private var grooveUndo: (pattern: Int, steps: [Float], length: Int, bpm: Float, swing: Float)?

    var genreCount: Int { Int(gp_genre_count()) }
    func genreName(_ g: Int) -> String { String(cString: gp_genre_name(Int32(g))) }
    var grooveCount: Int { Int(gp_groove_count()) }
    func grooveName(_ i: Int) -> String { String(cString: gp_groove_name(Int32(i))) }
    func grooveGenreOf(_ i: Int) -> Int { Int(gp_groove_genre(Int32(i))) }
    func grooves(in genre: Int) -> [Int] { (0..<grooveCount).filter { grooveGenreOf($0) == genre } }
    var canUndoGroove: Bool { grooveUndo != nil }

    /// Which pad plays each drum role, guessed from pad names (kits and user samples alike).
    func rolePads() -> [Int32] {
        var roles = [Int32](repeating: -1, count: Int(GP_R_COUNT.rawValue))
        func set(_ r: GPRole, _ p: Int) { if roles[Int(r.rawValue)] < 0 { roles[Int(r.rawValue)] = Int32(p) } }
        for p in 0..<Eng.padCount where Eng.hasSample(p) {
            let n = pads[p].name.lowercased()
            if n.contains("open") { set(GP_R_OPEN, p) }
            else if n.contains("hat") || n.contains("hh") { set(GP_R_HAT, p) }
            else if n.contains("kick") || n.contains("bd") || n == "pulse" { set(GP_R_KICK, p) }
            else if n.contains("snare") || n.contains("sd") { set(GP_R_SNARE, p) }
            else if n.contains("clap") || n.contains("snap") { set(GP_R_CLAP, p) }
            else if n.contains("rim") || n.contains("tick") || n.contains("cowbell") { set(GP_R_RIM, p) }
            else if n.contains("ride") || n.contains("cymbal") { set(GP_R_RIDE, p) }
            else if n.contains("808") || n.contains("bass") || n.contains("sub") || n.contains("acid") { set(GP_R_BASS, p) }
            else if n.contains("shaker") || n.contains("perc") || n.contains("conga") || n.contains("tom") || n.contains("brush") {
                if roles[Int(GP_R_PERC.rawValue)] < 0 { set(GP_R_PERC, p) } else { set(GP_R_PERC2, p) }
            }
        }
        // unnamed pads (slices, takes): fall back to the classic layout
        let fallback: [(GPRole, Int)] = [(GP_R_KICK, 0), (GP_R_SNARE, 1), (GP_R_HAT, 2), (GP_R_OPEN, 3),
                                         (GP_R_CLAP, 4), (GP_R_RIM, 5), (GP_R_PERC, 6), (GP_R_PERC2, 7)]
        for (r, p) in fallback where roles[Int(r.rawValue)] < 0 && Eng.hasSample(p) && !roles.contains(Int32(p)) { set(r, p) }
        return roles
    }

    private func rememberForUndo() {
        let p = Eng.currentPattern
        grooveUndo = (p, Eng.readSteps(p), Eng.patternLength(p), Eng.master(GP_M_BPM), Eng.master(GP_M_SWING))
    }

    func applyGroove(_ i: Int) {
        keepVariation()
        rememberForUndo()
        let roles = rolePads()
        roles.withUnsafeBufferPointer { gp_apply_groove(Int32(i), Int32(Eng.currentPattern), $0.baseAddress) }
        if grooveSetsTempo {
            Eng.setMaster(GP_M_BPM, gp_groove_bpm(Int32(i)))
            Eng.setMaster(GP_M_SWING, gp_groove_swing(Int32(i)))
        }
        lastGroove = grooveName(i)
        if !Eng.isPlaying { Eng.setPlaying(true) }
        changed()
    }

    /// Invents a fresh groove in the chosen genre.
    func rollGroove(genre: Int? = nil) {
        keepVariation()
        rememberForUndo()
        let g = genre ?? grooveGenre
        let roles = rolePads()
        var swing: Float = 0
        let bpm = roles.withUnsafeBufferPointer {
            gp_generate_groove(Int32(g), UInt32.random(in: 1...UInt32.max), variationAmount, Int32(Eng.currentPattern), $0.baseAddress, &swing)
        }
        if grooveSetsTempo && bpm > 0 {
            Eng.setMaster(GP_M_BPM, bpm)
            Eng.setMaster(GP_M_SWING, swing)
        }
        lastGroove = "New \(genreName(g)) groove"
        if !Eng.isPlaying { Eng.setPlaying(true) }
        changed()
    }

    func undoGroove() {
        guard let u = grooveUndo else { return }
        Eng.setPatternLength(u.pattern, u.length)
        Eng.applySteps(u.pattern, u.steps)
        Eng.setMaster(GP_M_BPM, u.bpm)
        Eng.setMaster(GP_M_SWING, u.swing)
        grooveUndo = nil
        lastGroove = nil
        changed()
    }

    // MARK: projects

    private func clearEngine() {
        Eng.setPlaying(false)
        gp_all_notes_off()
        for p in 0..<Eng.padCount {
            Eng.clearSample(p)
            gp_reset_pad_params(Int32(p))
            pads[p] = PadMeta(name: "", color: p / 2)
        }
        for a in 0..<Eng.patternCount { gp_clear_pattern(Int32(a)); Eng.setPatternLength(a, 16) }
        Eng.selectPattern(0)
        keepVariation()
        selectedPad = 0
    }

    /// kit == nil makes an empty project.
    func newProject(kit: Int?) {
        saveNow()
        store.beginNew()
        if let k = kit {
            applyKit(k)
            projectName = store.uniqueName(kitName(k) + " Beat")
        } else {
            clearEngine()
            Eng.setMaster(GP_M_BPM, 90)
            projectName = store.uniqueName("New Project")
        }
        changed()
        saveNow()
        showLibrary = false
        show("Created “\(projectName)”")
    }

    func openProject(_ id: String) {
        guard id != store.currentID else { showLibrary = false; return }
        saveNow()
        keepVariation()
        if store.load(id, into: self) {
            selectedPad = 0
            changed()
            showLibrary = false
            show("Opened “\(projectName)”")
        } else {
            show("Couldn't open that project")
        }
    }

    func duplicateProject(_ id: String) {
        if id == store.currentID { saveNow() }
        _ = store.duplicate(id)
        changed()
    }

    func renameProject(_ id: String, to name: String) {
        let n = name.trimmingCharacters(in: .whitespaces)
        guard !n.isEmpty else { return }
        if id == store.currentID { projectName = n; saveNow() } else { store.rename(id, to: n) }
        changed()
    }

    func deleteProject(_ id: String) {
        let wasCurrent = id == store.currentID
        store.delete(id)
        if wasCurrent {
            if let next = store.list().first, store.load(next.id, into: self) {
                show("Opened “\(projectName)”")
            } else {
                store.beginNew()
                clearEngine()
                projectName = "New Project"
                saveNow()
            }
        }
        changed()
    }

    // MARK: export

    func exportPatternAudio(loops: Int) {
        saveNow()
        guard let url = Exporter.patternWAV(state: self, pattern: Eng.currentPattern, loops: loops) else {
            show("Export failed"); return
        }
        share = ShareItems(urls: [url])
    }

    func exportPatternMIDI() {
        guard let url = Exporter.patternMIDI(state: self, pattern: Eng.currentPattern) else { show("Export failed"); return }
        share = ShareItems(urls: [url])
    }

    func exportSamples() {
        let urls = Exporter.samples(state: self)
        if urls.isEmpty { show("No samples to export"); return }
        share = ShareItems(urls: urls)
    }

    // MARK: misc

    func patternName(_ p: Int) -> String { ["A", "B", "C", "D", "E", "F", "G", "H"][max(0, min(7, p))] }

    func show(_ message: String) {
        toast = message
        toastToken &+= 1
        let token = toastToken
        Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 2_200_000_000)
            if let self, self.toastToken == token { self.toast = nil }
        }
    }
}
