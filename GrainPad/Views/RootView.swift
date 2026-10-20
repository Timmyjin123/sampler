import SwiftUI

/// Layout: slim top bar, mode switch, one fixed-height panel, then the pads.
/// Nothing scrolls, so nothing hides behind anything else.
struct RootView: View {
    @EnvironmentObject var state: AppState

    static let panelHeight: CGFloat = 236

    var body: some View {
        GeometryReader { geo in
            let wide = geo.size.width > geo.size.height && geo.size.width > 640
            ZStack(alignment: .top) {
                Theme.bg.ignoresSafeArea()
                if wide {
                    HStack(alignment: .top, spacing: 16) {
                        let side = min(geo.size.height, geo.size.width * 0.5)
                        pads.frame(width: side, height: side)
                        VStack(spacing: 10) {
                            TopBar()
                            modeBar
                            panel.frame(maxHeight: .infinity, alignment: .top)
                        }
                    }
                } else {
                    VStack(spacing: 10) {
                        TopBar()
                        modeBar
                        panel.frame(height: Self.panelHeight, alignment: .top)
                        pads.frame(maxHeight: .infinity)
                    }
                }
                if let t = state.toast {
                    Text(t)
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 14).padding(.vertical, 8)
                        .foregroundColor(.black)
                        .background(Capsule().fill(Theme.accent))
                        .shadow(color: .black.opacity(0.4), radius: 10, y: 4)
                        .padding(.top, 48)
                        .transition(.move(edge: .top).combined(with: .opacity))
                        .allowsHitTesting(false)
                }
            }
            .animation(.easeOut(duration: 0.2), value: state.toast)
        }
        .padding(.horizontal, 12)
        .padding(.top, 4)
        .padding(.bottom, 6)
        .background(Theme.bg.ignoresSafeArea())
        .sheet(isPresented: $state.showLibrary) { LibrarySheet() }
        .sheet(isPresented: $state.showPadSettings) { PadSettingsSheet() }
        .sheet(isPresented: $state.showSounds) { SoundBrowser() }
    }

    private var modeBar: some View {
        HStack(spacing: 2) {
            ForEach(Mode.allCases) { m in
                let on = state.mode == m
                Button { state.mode = m } label: {
                    Text(m.rawValue)
                        .font(.system(size: 11, weight: .bold, design: .rounded))
                        .tracking(0.8)
                        .frame(maxWidth: .infinity).frame(height: 30)
                        .foregroundColor(on ? Theme.text : Theme.faint)
                        .overlay(alignment: .bottom) {
                            Capsule().fill(on ? Theme.accent : Color.clear).frame(height: 2).padding(.horizontal, 24)
                        }
                }
                .buttonStyle(.plain)
            }
        }
    }

    @ViewBuilder
    private var panel: some View {
        switch state.mode {
        case .sound: SoundPanel()
        case .sequence: SequencePanel()
        case .fx: FXPanel()
        }
    }

    private var pads: some View {
        PadGrid(metas: state.pads, selected: state.selectedPad,
                showSteps: state.mode == .sequence,
                onPress: { pad in
                    Eng.trigger(pad)
                    state.select(pad)
                },
                onRelease: { pad in Eng.release(pad) })
    }
}
