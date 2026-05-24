// license:GPLv3+
//
// ControlsHintView.swift — translucent overlay that appears briefly
// over the playfield at table load to remind the player of the
// primary key bindings. Pinball-cabinet idiom: appear when needed,
// then get out of the way.
//
// Auto-shows when a Player becomes active (poll-based detection
// against vpx_settings_is_player_active) and fades out after a few
// seconds. Re-shows on the next table load.
//
// Bindings are hardcoded against VPX's standard mapping. If we later
// add rebinding support, this list should pull from the engine
// (Settings::m_propKey_*).

import SwiftUI
import AppKit
import Foundation


private struct Binding {
    let keys: String
    let description: String
}

private let primaryBindings: [Binding] = [
    Binding(keys: "⇧ L / ⇧ R", description: "Flippers"),
    Binding(keys: "Enter",     description: "Plunger"),
    Binding(keys: "Z  /",      description: "Nudge"),
    Binding(keys: "C",         description: "Insert coin"),
    Binding(keys: "S",         description: "Start game"),
    Binding(keys: "M",         description: "Menu"),
    Binding(keys: "Esc",       description: "Exit table"),
]

/// Process-lifetime singleton so the View-menu command in VPXApp can
/// reach the same model the ControlsHintView observes. Marked `final`
/// + private init to keep the single-source invariant.
final class ControlsHintModel: ObservableObject {
    static let shared = ControlsHintModel()

    @Published var isVisible = false

    private var hideTimer: Timer?

    private static let visibleDuration: TimeInterval = 15

    private init() {}

    deinit {
        hideTimer?.invalidate()
    }

    /// Called from the View-menu command. Public so the menu binding
    /// can drive it without an environment-object dance. Only entry
    /// point — the hint no longer auto-shows on table load.
    func toggle() {
        if isVisible {
            hideTimer?.invalidate()
            isVisible = false
        } else {
            isVisible = true
            hideTimer?.invalidate()
            hideTimer = Timer.scheduledTimer(withTimeInterval: Self.visibleDuration, repeats: false) { [weak self] _ in
                self?.isVisible = false
            }
        }
    }
}


struct ControlsHintView: View {
    @ObservedObject private var model = ControlsHintModel.shared

    var body: some View {
        card
            .opacity(model.isVisible ? 1 : 0)
            .animation(.easeInOut(duration: 0.35), value: model.isVisible)
            .allowsHitTesting(false)
    }

    private var card: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label("Controls", systemImage: "keyboard")
                .font(.headline)
                .foregroundStyle(.primary)
            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 4) {
                ForEach(0..<primaryBindings.count, id: \.self) { i in
                    GridRow {
                        Text(primaryBindings[i].keys)
                            .font(.system(.body, design: .monospaced))
                            .foregroundStyle(.primary)
                        Text(primaryBindings[i].description)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .font(.system(size: 13))
            Divider()
            Text("Press ⌘ I to toggle")
                .font(.caption2)
                .foregroundStyle(.tertiary)
        }
        .padding(16)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .strokeBorder(Color.secondary.opacity(0.25), lineWidth: 0.5)
        )
        .shadow(color: .black.opacity(0.3), radius: 8, y: 2)
    }
}
