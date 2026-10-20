import SwiftUI
import UIKit

enum Theme {
    static let bg = Color(red: 0.055, green: 0.055, blue: 0.065)
    static let panel = Color(red: 0.095, green: 0.095, blue: 0.11)
    static let raised = Color(red: 0.14, green: 0.14, blue: 0.16)
    static let line = Color.white.opacity(0.08)
    static let text = Color(white: 0.92)
    static let dim = Color(white: 0.55)
    static let faint = Color(white: 0.32)
    static let accent = Color(red: 1.0, green: 0.78, blue: 0.24)      // warm amber
    static let rec = Color(red: 1.0, green: 0.27, blue: 0.27)
    static let go = Color(red: 0.38, green: 0.92, blue: 0.56)

    /// Pad colours, chosen to stay readable on near-black.
    static let padUIColors: [UIColor] = [
        UIColor(red: 1.00, green: 0.42, blue: 0.32, alpha: 1),   // coral
        UIColor(red: 1.00, green: 0.71, blue: 0.24, alpha: 1),   // amber
        UIColor(red: 0.98, green: 0.90, blue: 0.36, alpha: 1),   // lemon
        UIColor(red: 0.45, green: 0.90, blue: 0.48, alpha: 1),   // mint
        UIColor(red: 0.30, green: 0.85, blue: 0.85, alpha: 1),   // teal
        UIColor(red: 0.38, green: 0.62, blue: 1.00, alpha: 1),   // sky
        UIColor(red: 0.66, green: 0.50, blue: 1.00, alpha: 1),   // violet
        UIColor(red: 1.00, green: 0.45, blue: 0.75, alpha: 1),   // pink
    ]
    static func padColor(_ index: Int) -> Color { Color(padUIColors[index % padUIColors.count]) }

    static let mono = Font.system(.caption, design: .monospaced).weight(.medium)
    static let label = Font.system(size: 10, weight: .semibold, design: .rounded)
    static let value = Font.system(size: 11, weight: .medium, design: .monospaced)
}
