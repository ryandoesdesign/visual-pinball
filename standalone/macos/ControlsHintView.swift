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

    private var pollTimer: Timer?
    private var hideTimer: Timer?
    private var wasActive = false

    private static let visibleDuration: TimeInterval = 15

    private init() {
        wasActive = vpx_settings_is_player_active() != 0
        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            self?.tick()
        }
        // No NSEvent monitor here: the View-menu item in VPXApp.swift
        // provides the F2 shortcut. Menu shortcuts intercept before
        // the playfield gets the event, so we don't need a per-window
        // monitor.
    }

    deinit {
        pollTimer?.invalidate()
        hideTimer?.invalidate()
    }

    private func tick() {
        let isActive = vpx_settings_is_player_active() != 0
        if isActive && !wasActive {
            // Player just became active — show the hint.
            showThenAutoHide()
        }
        wasActive = isActive
    }

    /// Called from the View-menu command. Public so the menu binding
    /// can drive it without an environment-object dance.
    func toggle() {
        if isVisible {
            hideTimer?.invalidate()
            isVisible = false
        } else {
            showThenAutoHide()
        }
    }

    private func showThenAutoHide() {
        isVisible = true
        hideTimer?.invalidate()
        hideTimer = Timer.scheduledTimer(withTimeInterval: Self.visibleDuration, repeats: false) { [weak self] _ in
            self?.isVisible = false
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
