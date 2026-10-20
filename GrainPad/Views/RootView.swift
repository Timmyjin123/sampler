import SwiftUI

struct RootView: View {
    @EnvironmentObject var state: AppState

    var body: some View {
        GeometryReader { geo in
            let wide = geo.size.width > geo.size.height && geo.size.width > 640
            ZStack(alignment: .top) {
                Theme.bg.ignoresSafeArea()
                if wide {
                    HStack(alignment: .top, spacing: 16) {
                        let side = min(geo.size.height - 8, geo.size.width * 0.48)
                        pads.frame(width: side, height: side)
                        VStack(spacing: 10) {
                            TopBar()
                            tabs
                            panel
                        }
                    }
                } else {
                    VStack(spacing: 10) {
                        TopBar()
                        tabs
                        panel
                        pads
                            .frame(height: min(geo.size.width, geo.size.height * 0.44))
                    }
                }
                if let t = state.toast {
                    Text(t)
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .padding(.horizontal, 14).padding(.vertical, 8)
                        .foregroundColor(.black)
                        .background(Capsule().fill(Theme.accent))
                        .shadow(color: .black.opacity(0.4), radius: 10, y: 4)
                        .padding(.top, 84)
                        .transition(.move(edge: .top).combined(with: .opacity))
                        .allowsHitTesting(false)
                }
            }
            .animation(.easeOut(duration: 0.2), value: state.toast)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Theme.bg.ignoresSafeArea())
    }

    private var tabs: some View {
        Segmented(items: PanelTab.allCases.map { ($0, $0.rawValue) }, selection: $state.tab)
    }

    private var panel: some View {
        ScrollView(.vertical, showsIndicators: false) {
            Group {
                switch state.tab {
                case .sample: SamplePanel()
                case .steps: StepsPanel()
                case .fx: FXPanel()
                }
            }
            .padding(.top, 2)
        }
        .frame(maxHeight: .infinity)
    }

    private var pads: some View {
        PadGrid(metas: state.pads, selected: state.selectedPad,
                onPress: { pad in
                    Eng.trigger(pad)
                    state.select(pad)
                },
                onRelease: { pad in Eng.release(pad) })
    }
}
