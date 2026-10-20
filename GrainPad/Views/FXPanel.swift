import SwiftUI

struct FXPanel: View {
    @EnvironmentObject var state: AppState

    private static let delayNames = ["1/32", "1/16", "3/32", "1/8", "3/16", "1/4", "3/8", "1/2"]

    var body: some View {
        let _ = state.revision
        let m = { (p: GPMasterParam) in state.masterBinding(p) }

        VStack(spacing: 8) {
            HStack(spacing: 6) {
                ForEach(1...4, id: \.self) { i in
                    HoldButton(onPress: { Eng.setMaster(GP_M_REPEAT, Float(i)) },
                               onRelease: { _ in
                                   if Int(Eng.master(GP_M_REPEAT).rounded()) == i { Eng.setMaster(GP_M_REPEAT, 0) }
                               }) { pressed in
                        VStack(spacing: 1) {
                            Text(["", "1/4", "1/8", "1/16", "1/32"][i]).font(.system(size: 13, weight: .heavy, design: .rounded))
                            Text("STUTTER").font(.system(size: 7, weight: .bold, design: .rounded)).opacity(0.6)
                        }
                        .frame(maxWidth: .infinity).frame(height: 44)
                        .foregroundColor(pressed ? .black : Theme.text)
                        .background(RoundedRectangle(cornerRadius: 9).fill(pressed ? Theme.padColor(7) : Theme.raised))
                    }
                }
                SampleRecordButton(source: .resample, compact: true)
                    .frame(height: 44)
                Menu {
                    ForEach(AppState.fxPresets) { p in
                        Button(p.name) { state.applyFX(p) }
                    }
                } label: {
                    VStack(spacing: 1) {
                        Image(systemName: "slider.horizontal.3").font(.system(size: 13, weight: .bold))
                        Text("PRESET").font(.system(size: 7, weight: .bold, design: .rounded)).opacity(0.6)
                    }
                    .frame(width: 50, height: 44)
                    .foregroundColor(Theme.text)
                    .background(RoundedRectangle(cornerRadius: 9).fill(Theme.raised))
                }
            }

            HStack(alignment: .top, spacing: 0) {
                Knob(label: "FILTER", value: m(GP_M_FILTER), range: 0...1, defaultValue: 0.5, bipolar: true, color: Theme.padColor(4),
                     format: { v in abs(v - 0.5) < 0.015 ? "off" : (v < 0.5 ? "LP \(Int((0.5 - v) * 200))" : "HP \(Int((v - 0.5) * 200))") })
                Knob(label: "RESO", value: m(GP_M_RESO), range: 0...1, defaultValue: 0.2, color: Theme.padColor(4), format: Fmt.pct)
                Knob(label: "DRIVE", value: m(GP_M_DRIVE), range: 0...1, defaultValue: 0, color: Theme.padColor(0), format: Fmt.pct)
                Knob(label: "CRUSH", value: m(GP_M_CRUSH), range: 0...1, defaultValue: 0, color: Theme.padColor(0), format: Fmt.pct)
                Knob(label: "VOLUME", value: m(GP_M_VOLUME), range: 0...1, defaultValue: 0.68, color: Theme.text, format: Fmt.gainDb)
            }

            HStack(alignment: .top, spacing: 0) {
                Knob(label: "DELAY", value: m(GP_M_DELAY_TIME), range: 0...7, defaultValue: 3, color: Theme.padColor(5),
                     format: { Self.delayNames[min(7, max(0, Int($0.rounded())))] })
                Knob(label: "FEEDBACK", value: m(GP_M_DELAY_FB), range: 0...0.95, defaultValue: 0.35, color: Theme.padColor(5), format: Fmt.pct)
                Knob(label: "TONE", value: m(GP_M_DELAY_TONE), range: 0...1, defaultValue: 0.5, color: Theme.padColor(5), format: Fmt.pct)
                Knob(label: "REVERB", value: m(GP_M_REVERB_SIZE), range: 0...1, defaultValue: 0.5, color: Theme.padColor(6), format: Fmt.pct)
                Knob(label: "DAMP", value: m(GP_M_REVERB_DAMP), range: 0...1, defaultValue: 0.4, color: Theme.padColor(6), format: Fmt.pct)
            }
            Spacer(minLength: 0)
        }
    }
}
