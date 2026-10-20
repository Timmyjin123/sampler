import SwiftUI

@main
struct GrainPadApp: App {
    @StateObject private var state = AppState()
    @StateObject private var ticker = Ticker()
    @Environment(\.scenePhase) private var phase

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(state)
                .environmentObject(ticker)
                .preferredColorScheme(.dark)
                .onAppear { state.boot() }
        }
        .onChange(of: phase) { p in
            if p == .background || p == .inactive { state.saveNow() }
        }
    }
}
