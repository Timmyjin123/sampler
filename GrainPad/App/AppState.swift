import SwiftUI
import Combine

enum PanelTab: String, CaseIterable, Identifiable {
    case sample = "SAMPLE", steps = "STEPS", fx = "FX"
    var id: String { rawValue }
}

struct PadMeta: Codable, Equatable {
    var name: String
    var color: Int
}

@MainActor
final class AppState: ObservableObject {
    @Published var selectedPad = 0
    @Published var tab: PanelTab = .sample
    @Published var pads: [PadMeta]
    /// Bumped whenever engine-side state changes, so views that read the
    /// engine directly re-render.
    @Published var revision = 0
    @Published var recordingSource: GPRecordSource? = nil
    @Published var recordLatched = false
    @Published var toast: String? = nil
    @Published var stepPage = 0

    // variations
    @Published var variations: [[Float]] = []
    @Published var variationOriginal: [Float]? = nil
    @Published var activeVariation: Int? = nil
    @Published var variationAmount: Float = 0.5

    let audio = GPAudio.shared
    private var clipboard: (params: [Float], pad: Int, meta: PadMeta)?
    private var dirty = false
    private var timers: [Timer] = []
    private var takeCounter = 1
    private var toastToken = 0
    let store = ProjectStore()

    init() {
        pads = (0..<Eng.padCount).map { PadMeta(name: "", color: $0 / 2) }
    }

    // MARK: lifecycle

    private var booted = false

    func boot() {
        guard !booted else { return }
        booted = true
        do { try audio.start() } catch { show("Audio failed to start: \(error.localizedDescription)") }
        if !store.load(into: self) {
            loadStarterKit(announce: false)
        }
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

    private var pendingStart = false

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

    func cancelRecord() {
        audio.cancelRecording()
        recordingSource = nil
        recordLatched = false
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

    /// Chops the selected pad onto pads starting at it, and (for smart chops)
    /// writes a pattern that replays the original loop.
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
            msg += " · pattern \(patternName(target))"
        }
        show(msg)
        changed()
        saveNow()
    }

    func makeVariations() {
        let pattern = Eng.currentPattern
        if variationOriginal == nil { variationOriginal = Eng.readSteps(pattern) }
        // generate from the original, not from a variation already applied
        let current = Eng.readSteps(pattern)
        if let orig = variationOriginal { Eng.applySteps(pattern, orig) }
        let base = UInt32.random(in: 1...UInt32.max - 8)
        variations = (0..<4).map { i -> [Float] in
            var out = [Float](repeating: 0, count: Eng.padCount * Eng.maxSteps)
            let amount = min(1, max(0.05, variationAmount * (0.75 + 0.2 * Float(i))))
            out.withUnsafeMutableBufferPointer { gp_make_variation(Int32(pattern), base + UInt32(i), amount, $0.baseAddress) }
            return out
        }
        if activeVariation == nil { Eng.applySteps(pattern, current) }
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

    // MARK: projects

    func newProject() {
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
        store.resetFiles()
        changed()
        saveNow()
    }

    func loadStarterKit(announce: Bool = true) {
        Eng.setPlaying(false)
        gp_all_notes_off()
        gp_load_starter_kit()
        let colors = [0, 1, 2, 2, 7, 3, 0, 3, 4, 4, 5, 5, 6, 7, 6, 6]
        for p in 0..<16 {
            pads[p] = PadMeta(name: String(cString: gp_starter_kit_name(Int32(p))), color: colors[p])
        }
        keepVariation()
        selectedPad = 0
        store.resetFiles()
        changed()
        saveNow()
        if announce { show("Starter kit loaded — press play") }
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
