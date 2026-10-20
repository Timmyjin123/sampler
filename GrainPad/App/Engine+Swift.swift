import Foundation

/// Thin Swift wrapper over the C engine API.
enum Eng {
    static let padCount = Int(GP_NUM_PADS)
    static let patternCount = Int(GP_NUM_PATTERNS)
    static let maxSteps = Int(GP_MAX_STEPS)

    // MARK: parameters
    static func pad(_ pad: Int, _ p: GPPadParam) -> Float {
        gp_get_pad_param(Int32(pad), Int32(p.rawValue))
    }
    static func setPad(_ pad: Int, _ p: GPPadParam, _ v: Float) {
        gp_set_pad_param(Int32(pad), Int32(p.rawValue), v)
    }
    static func master(_ p: GPMasterParam) -> Float {
        gp_get_master(Int32(p.rawValue))
    }
    static func setMaster(_ p: GPMasterParam, _ v: Float) {
        gp_set_master(Int32(p.rawValue), v)
    }

    // MARK: performance / transport
    static func trigger(_ pad: Int, velocity: Float = 1) { gp_trigger(Int32(pad), velocity) }
    static func release(_ pad: Int) { gp_release(Int32(pad)) }
    static var isPlaying: Bool { gp_is_playing() != 0 }
    static var isRecording: Bool { gp_is_recording() != 0 }
    static func setPlaying(_ on: Bool) { gp_set_playing(on ? 1 : 0) }
    static func setRecording(_ on: Bool) { gp_set_recording(on ? 1 : 0) }
    static var currentPattern: Int { Int(gp_current_pattern()) }
    static var queuedPattern: Int { Int(gp_queued_pattern()) }
    static var currentStep: Int { Int(gp_current_step()) }
    static func selectPattern(_ p: Int) { gp_select_pattern(Int32(p)) }
    static func patternLength(_ p: Int) -> Int { Int(gp_get_pattern_length(Int32(p))) }
    static func setPatternLength(_ p: Int, _ n: Int) { gp_set_pattern_length(Int32(p), Int32(n)) }
    static func step(_ pattern: Int, _ pad: Int, _ s: Int) -> Float { gp_get_step(Int32(pattern), Int32(pad), Int32(s)) }
    static func setStep(_ pattern: Int, _ pad: Int, _ s: Int, _ v: Float) {
        gp_set_step(Int32(pattern), Int32(pad), Int32(s), v)
    }
    static func readSteps(_ pattern: Int) -> [Float] {
        var out = [Float](repeating: 0, count: padCount * maxSteps)
        out.withUnsafeMutableBufferPointer { gp_read_steps(Int32(pattern), $0.baseAddress) }
        return out
    }
    static func applySteps(_ pattern: Int, _ steps: [Float]) {
        steps.withUnsafeBufferPointer { gp_apply_steps(Int32(pattern), $0.baseAddress) }
    }
    static func patternIsEmpty(_ pattern: Int) -> Bool {
        !readSteps(pattern).contains { $0 > 0 }
    }

    // MARK: samples
    static func hasSample(_ pad: Int) -> Bool { gp_pad_has_sample(Int32(pad)) != 0 }
    static func sampleID(_ pad: Int) -> Int { Int(gp_pad_sample_id(Int32(pad))) }
    static func frames(_ pad: Int) -> Int { Int(gp_pad_frames(Int32(pad))) }
    static func sampleRate(_ pad: Int) -> Double { gp_pad_sample_rate(Int32(pad)) }
    static func duration(_ pad: Int) -> Double {
        let sr = sampleRate(pad); return sr > 0 ? Double(frames(pad)) / sr : 0
    }
    static func peaks(_ pad: Int, bins: Int) -> [Float] {
        var out = [Float](repeating: 0, count: bins * 2)
        out.withUnsafeMutableBufferPointer { gp_pad_peaks(Int32(pad), $0.baseAddress, Int32(bins)) }
        return out
    }
    static func clearSample(_ pad: Int) { gp_clear_pad_sample(Int32(pad)) }
    static func share(_ dst: Int, from src: Int) { gp_share_pad_sample(Int32(dst), Int32(src)) }
    static func detectBPM(_ pad: Int) -> Float { gp_detect_bpm(Int32(pad)) }

    // MARK: feedback
    static func activity(_ pad: Int) -> Float { gp_pad_activity(Int32(pad)) }
    static func playhead(_ pad: Int) -> Float { gp_pad_playhead(Int32(pad)) }
    static func hits(_ pad: Int) -> UInt32 { gp_pad_hit_counter(Int32(pad)) }
    static func outputPeak(_ ch: Int) -> Float { gp_output_peak(Int32(ch)) }
}
