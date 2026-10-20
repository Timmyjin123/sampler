import SwiftUI
import UIKit

/// Builds export files in a temporary folder for the share sheet.
@MainActor
enum Exporter {
    static func folder() -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("Export", isDirectory: true)
        try? FileManager.default.removeItem(at: url)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    static func safe(_ s: String) -> String {
        let bad = CharacterSet(charactersIn: "/\\:?%*|\"<>")
        let cleaned = s.components(separatedBy: bad).joined(separator: "-").trimmingCharacters(in: .whitespaces)
        return cleaned.isEmpty ? "GrainPad" : cleaned
    }

    /// Offline bounce of a pattern to a stereo WAV.
    static func patternWAV(state: AppState, pattern: Int, loops: Int) -> URL? {
        let bpm = Int(Eng.master(GP_M_BPM).rounded())
        let url = folder().appendingPathComponent(
            "\(safe(state.projectName)) - Pattern \(state.patternName(pattern)) - \(bpm) BPM.wav")
        do { try state.audio.bouncePattern(Int32(pattern), loops: Int32(loops), to: url) } catch { return nil }
        return url
    }

    /// Every distinct sample in the project, numbered by pad.
    static func samples(state: AppState) -> [URL] {
        let dir = folder()
        var seen = Set<Int>()
        var urls: [URL] = []
        for p in 0..<Eng.padCount where Eng.hasSample(p) {
            let id = Eng.sampleID(p)
            if seen.contains(id) { continue }
            seen.insert(id)
            let name = state.pads[p].name.isEmpty ? "Pad \(p + 1)" : state.pads[p].name
            let url = dir.appendingPathComponent(String(format: "%02d %@.wav", p + 1, safe(name)))
            if (try? state.audio.writePad(Int32(p), to: url)) != nil { urls.append(url) }
        }
        return urls
    }

    /// Standard MIDI file of a pattern: pad 1 = C1 (36), the Ableton Drum Rack layout.
    static func patternMIDI(state: AppState, pattern: Int) -> URL? {
        let ppq = 96, stepTicks = 24
        let bpm = Double(Eng.master(GP_M_BPM))
        let swing = Double(Eng.master(GP_M_SWING))
        let len = Eng.patternLength(pattern)
        var events: [(tick: Int, bytes: [UInt8])] = []
        for pad in 0..<Eng.padCount {
            for s in 0..<len {
                let v = Eng.step(pattern, pad, s)
                guard v > 0 else { continue }
                let t = s * stepTicks + (s % 2 == 1 ? Int(swing * Double(stepTicks)) : 0)
                let note = UInt8(36 + pad)
                let vel = UInt8(max(1, min(127, Int((v * 127).rounded()))))
                events.append((t, [0x99, note, vel]))
                events.append((t + stepTicks - 4, [0x89, note, 0]))
            }
        }
        events.sort { $0.tick != $1.tick ? $0.tick < $1.tick : $0.bytes[0] < $1.bytes[0] }   // offs first

        var track: [UInt8] = []
        func vlq(_ v: Int) {
            var value = v, buffer = [UInt8(value & 0x7F)]
            value >>= 7
            while value > 0 { buffer.insert(UInt8(value & 0x7F) | 0x80, at: 0); value >>= 7 }
            track += buffer
        }
        let mpq = Int(60_000_000 / bpm)
        vlq(0); track += [0xFF, 0x51, 0x03, UInt8((mpq >> 16) & 0xFF), UInt8((mpq >> 8) & 0xFF), UInt8(mpq & 0xFF)]
        vlq(0); track += [0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08]
        var last = 0
        for e in events { vlq(e.tick - last); track += e.bytes; last = e.tick }
        let end = len * stepTicks
        vlq(max(0, end - last)); track += [0xFF, 0x2F, 0x00]

        func be32(_ v: Int) -> [UInt8] { [UInt8((v >> 24) & 0xFF), UInt8((v >> 16) & 0xFF), UInt8((v >> 8) & 0xFF), UInt8(v & 0xFF)] }
        var data: [UInt8] = Array("MThd".utf8) + be32(6) + [0, 0, 0, 1, UInt8(ppq >> 8), UInt8(ppq & 0xFF)]
        data += Array("MTrk".utf8) + be32(track.count) + track

        let url = folder().appendingPathComponent("\(safe(state.projectName)) - Pattern \(state.patternName(pattern)).mid")
        do { try Data(data).write(to: url) } catch { return nil }
        return url
    }
}

/// UIKit share sheet (AirDrop, Files, Ableton via Files, Messages…).
struct ShareSheet: UIViewControllerRepresentable {
    let items: [Any]
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }
    func updateUIViewController(_ vc: UIActivityViewController, context: Context) {}
}

struct ShareItems: Identifiable {
    let id = UUID()
    let urls: [URL]
}
