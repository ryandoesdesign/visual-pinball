// license:GPLv3+
//
// ViewSettingsView.swift — SwiftUI port of the C++ Point of View
// Settings page (src/ui/live/ingameui/PointOfViewSettingsPage.cpp).
//
// Preset-only UI: 5 named camera setups laid out as cards, mirroring
// Pro Pinball: Timeshock!'s "View Options" screen. Tapping a card
// applies every view property at once via the bridge; no manual
// sliders this slice. The active preset is highlighted by comparing
// engine state to each preset's stored values within a small
// tolerance.

import SwiftUI
import Foundation


// Camera preset. Values are starting estimates matched to the visual
// character of Timeshock's preset thumbnails — adjust in code as the
// in-game result demands.
private struct ViewPreset: Identifiable, Hashable {
    let id: Int
    let name: String
    let description: String
    let fov: Float
    let lookAt: Float
    let layback: Float
    let scaleX: Float
    let scaleY: Float
    let scaleZ: Float
    let hOfs: Float
    let vOfs: Float
    let rotation: Float

    var systemImage: String {
        switch id {
        case 0: return "arrow.down.app"            // Top-Down
        case 1: return "arrow.down.right.square"   // Bird's Eye
        case 2: return "rectangle.center.inset.filled" // Standard
        case 3: return "person.fill.viewfinder"    // Player POV
        case 4: return "gamecontroller.fill"       // Cabinet
        default: return "questionmark.square"
        }
    }
}

private let presets: [ViewPreset] = [
    ViewPreset(
        id: 0, name: "Standard", description: "Wide FOV, slight forward tilt",
        fov: 60, lookAt: 42, layback: 0,
        scaleX: 1.2, scaleY: 1.0, scaleZ: 1.0,
        hOfs: 0, vOfs: 0, rotation: 0
    ),
]


/// Registers the first (default) preset with the engine as the
/// "default view" — every table loaded from now on starts with these
/// values applied to its ViewSetup, replacing whatever the table
/// stored. No polling, no race, no retries: the engine applies the
/// preset as part of Player construction (see
/// vpx_view_internal_apply_default_preset_on_load in
/// SettingsBridge.cpp and the call site in src/core/player.cpp).
///
/// Lifecycle: invoked from VPXAppDelegate.applicationDidBecomeActive.
/// Idempotent — repeated calls just refresh the registered values.
enum ViewPresetAutoApplier {
    static func install() {
        guard let p = presets.first else { return }
        vpx_view_set_default_preset(
            1, // enabled
            p.fov, p.lookAt, p.layback,
            p.scaleX, p.scaleY, p.scaleZ,
            p.hOfs, p.vOfs, p.rotation
        )
    }
}


private final class ViewSettingsModel: ObservableObject {
    @Published var isActive: Bool = false
    @Published var activePresetId: Int? = nil

    // Individual properties — drive the Advanced disclosure's sliders.
    @Published var fov: Float = 45
    @Published var lookAt: Float = 0.25
    @Published var layback: Float = 0
    @Published var scaleX: Float = 1
    @Published var scaleY: Float = 1
    @Published var scaleZ: Float = 1
    @Published var hOfs: Float = 0
    @Published var vOfs: Float = 0
    @Published var rotation: Float = 0

    /// UI-side preference: when true, dragging any of scaleX/Y/Z drags
    /// the other two by the same delta. Matches the ImGui page's
    /// `m_lockScale`. Not persisted.
    @Published var lockScale: Bool = true

    private var timer: Timer?

    init() {
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            self?.refresh()
        }
    }

    deinit { timer?.invalidate() }

    func refresh() {
        isActive = vpx_settings_is_player_active() != 0
        guard isActive else { activePresetId = nil; return }

        fov      = vpx_view_get(VPX_VIEW_FOV)
        lookAt   = vpx_view_get(VPX_VIEW_LOOK_AT)
        layback  = vpx_view_get(VPX_VIEW_LAYBACK)
        scaleX   = vpx_view_get(VPX_VIEW_SCALE_X)
        scaleY   = vpx_view_get(VPX_VIEW_SCALE_Y)
        scaleZ   = vpx_view_get(VPX_VIEW_SCALE_Z)
        hOfs     = vpx_view_get(VPX_VIEW_HOFS)
        vOfs     = vpx_view_get(VPX_VIEW_VOFS)
        rotation = vpx_view_get(VPX_VIEW_ROTATION)

        // Detect which preset (if any) the current state matches.
        // Tolerances let small float-normalisation drift still match.
        activePresetId = presets.first(where: { p in
            abs(p.fov      - fov)      < 1.0 &&
            abs(p.lookAt   - lookAt)   < 2.0 &&
            abs(p.scaleX   - scaleX)   < 0.05 &&
            abs(p.scaleY   - scaleY)   < 0.05 &&
            abs(p.scaleZ   - scaleZ)   < 0.05 &&
            abs(p.rotation - rotation) < 1.0
        })?.id
    }

    func apply(_ preset: ViewPreset) {
        vpx_view_set(VPX_VIEW_FOV,      preset.fov)
        vpx_view_set(VPX_VIEW_LOOK_AT,  preset.lookAt)
        vpx_view_set(VPX_VIEW_LAYBACK,  preset.layback)
        vpx_view_set(VPX_VIEW_SCALE_X,  preset.scaleX)
        vpx_view_set(VPX_VIEW_SCALE_Y,  preset.scaleY)
        vpx_view_set(VPX_VIEW_SCALE_Z,  preset.scaleZ)
        vpx_view_set(VPX_VIEW_HOFS,     preset.hOfs)
        vpx_view_set(VPX_VIEW_VOFS,     preset.vOfs)
        vpx_view_set(VPX_VIEW_ROTATION, preset.rotation)
        activePresetId = preset.id   // optimistic — refresh() will reconcile
    }

    // --- Single-property mutators (Advanced sliders) ---

    private func push(_ prop: vpx_view_property_t, _ value: Float) {
        vpx_view_set(prop, value)
    }

    func commitFov     (_ v: Float) { fov = v;      push(VPX_VIEW_FOV, v) }
    func commitLookAt  (_ v: Float) { lookAt = v;   push(VPX_VIEW_LOOK_AT, v) }
    func commitLayback (_ v: Float) { layback = v;  push(VPX_VIEW_LAYBACK, v) }
    func commitHOfs    (_ v: Float) { hOfs = v;     push(VPX_VIEW_HOFS, v) }
    func commitVOfs    (_ v: Float) { vOfs = v;     push(VPX_VIEW_VOFS, v) }
    func commitRotation(_ v: Float) { rotation = v; push(VPX_VIEW_ROTATION, v) }

    // Scale commits respect the XYZ-lock toggle.
    func commitScaleX(_ v: Float) {
        let d = v - scaleX
        scaleX = v
        push(VPX_VIEW_SCALE_X, v)
        if lockScale {
            scaleY = clamp(scaleY + d, 0.5, 1.5); push(VPX_VIEW_SCALE_Y, scaleY)
            scaleZ = clamp(scaleZ + d, 0.5, 1.5); push(VPX_VIEW_SCALE_Z, scaleZ)
        }
    }
    func commitScaleY(_ v: Float) {
        let d = v - scaleY
        scaleY = v
        push(VPX_VIEW_SCALE_Y, v)
        if lockScale {
            scaleX = clamp(scaleX + d, 0.5, 1.5); push(VPX_VIEW_SCALE_X, scaleX)
            scaleZ = clamp(scaleZ + d, 0.5, 1.5); push(VPX_VIEW_SCALE_Z, scaleZ)
        }
    }
    func commitScaleZ(_ v: Float) {
        let d = v - scaleZ
        scaleZ = v
        push(VPX_VIEW_SCALE_Z, v)
        if lockScale {
            scaleX = clamp(scaleX + d, 0.5, 1.5); push(VPX_VIEW_SCALE_X, scaleX)
            scaleY = clamp(scaleY + d, 0.5, 1.5); push(VPX_VIEW_SCALE_Y, scaleY)
        }
    }

    private func clamp(_ x: Float, _ lo: Float, _ hi: Float) -> Float {
        min(max(x, lo), hi)
    }
}


struct ViewSettingsView: View {
    @StateObject private var model = ViewSettingsModel()
    @State private var advancedExpanded: Bool = false

    private static let cardColumns = [
        GridItem(.adaptive(minimum: 140), spacing: 12)
    ]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                if !model.isActive {
                    Text("Load a table to choose a view.")
                        .foregroundStyle(.secondary)
                } else {
                    Text("Tap a preset to switch the camera view. Changes apply instantly.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                    LazyVGrid(columns: Self.cardColumns, spacing: 12) {
                        ForEach(presets) { preset in
                            PresetCard(
                                preset: preset,
                                isActive: model.activePresetId == preset.id,
                                onTap: { model.apply(preset) }
                            )
                        }
                    }

                    DisclosureGroup("Advanced", isExpanded: $advancedExpanded) {
                        advancedControls
                            .padding(.top, 8)
                    }
                    .font(.headline)
                }
            }
            .padding()
        }
        .disabled(!model.isActive)
    }

    private var advancedControls: some View {
        VStack(alignment: .leading, spacing: 16) {
            advancedSection("Camera") {
                FloatSlider(label: "FOV",      value: model.fov,
                            range: 10...120,   step: 0.5,  format: "%.1f°",
                            description: "Field of view. Lower values zoom in (telephoto); higher values widen the perspective (fisheye).",
                            onCommit: model.commitFov)
                FloatSlider(label: "Look at",  value: model.lookAt,
                            range: -50...100,  step: 0.5,  format: "%.1f%%",
                            description: "Where the camera aims along the playfield. 0% aims at the flippers, higher values aim further up the table.",
                            onCommit: model.commitLookAt)
                FloatSlider(label: "Rotation", value: model.rotation,
                            range: -180...180, step: 1,    format: "%.0f°",
                            description: "Rotates the rendered image around the screen centre. Useful for portrait/cabinet displays.",
                            onCommit: model.commitRotation)
                // Layback is intentionally omitted — it only affects
                // the engine's legacy view mode (see ViewSetup.cpp:610
                // `if (isLegacy && ...)`). On modern Camera / Window
                // modes (which our presets target) the field is read
                // and silently ignored, so a slider would mislead the
                // user about whether their drag is doing anything.
            }

            advancedSection("Scale") {
                VStack(alignment: .leading, spacing: 2) {
                    Toggle("Lock XYZ scale", isOn: $model.lockScale)
                    Text("When on, dragging any axis drags the other two by the same amount, keeping the table's proportions.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                FloatSlider(label: "Scale X",  value: model.scaleX,
                            range: 0.5...1.5,  step: 0.01, format: "%.2f×",
                            description: "Horizontal stretch. Below 1.0 narrows the table; above 1.0 widens it.",
                            onCommit: model.commitScaleX)
                FloatSlider(label: "Scale Y",  value: model.scaleY,
                            range: 0.5...1.5,  step: 0.01, format: "%.2f×",
                            description: "Vertical stretch. Below 1.0 shortens the table; above 1.0 lengthens it.",
                            onCommit: model.commitScaleY)
                FloatSlider(label: "Scale Z",  value: model.scaleZ,
                            range: 0.5...1.5,  step: 0.01, format: "%.2f×",
                            description: "Depth / height scale. Affects how tall ramps, posts and bumpers appear.",
                            onCommit: model.commitScaleZ)
            }

            advancedSection("Frustum offset") {
                FloatSlider(label: "Horizontal", value: model.hOfs,
                            range: -50...50,    step: 0.5, format: "%.1f",
                            description: "Shifts the view sideways without moving the camera. Useful for off-centre projectors or to fine-tune framing.",
                            onCommit: model.commitHOfs)
                FloatSlider(label: "Vertical",   value: model.vOfs,
                            range: -50...50,    step: 0.5, format: "%.1f",
                            description: "Shifts the view up or down without tilting the camera. Compensates for screens mounted high or low.",
                            onCommit: model.commitVOfs)
            }
        }
    }

    private func advancedSection<Content: View>(
        _ title: String, @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            content()
        }
    }
}


/// Slider + value-readout row, with an optional description line
/// below the slider. Streams while dragging; the bridge queues each
/// push as a render-thread command so the throttling happens at the
/// engine boundary, not in the UI.
private struct FloatSlider: View {
    let label: String
    let value: Float
    let range: ClosedRange<Float>
    let step: Float
    let format: String
    var description: String? = nil
    let onCommit: (Float) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(label)
                Spacer()
                Text(String(format: format, value))
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }
            Slider(
                value: Binding(get: { value }, set: { onCommit($0) }),
                in: range,
                step: step
            )
            if let description {
                Text(description)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }
}


private struct PresetCard: View {
    let preset: ViewPreset
    let isActive: Bool
    let onTap: () -> Void

    var body: some View {
        Button(action: onTap) {
            VStack(alignment: .leading, spacing: 8) {
                Image(systemName: preset.systemImage)
                    .font(.system(size: 36, weight: .light))
                    .foregroundStyle(isActive ? .white : .accentColor)
                    .frame(maxWidth: .infinity, alignment: .center)
                    .padding(.top, 12)
                Text(preset.name)
                    .font(.headline)
                    .foregroundStyle(isActive ? .white : .primary)
                Text(preset.description)
                    .font(.caption)
                    .foregroundStyle(isActive ? .white.opacity(0.85) : .secondary)
                    .lineLimit(2, reservesSpace: true)
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: 10)
                    .fill(isActive ? Color.accentColor : Color.clear)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .strokeBorder(isActive ? Color.accentColor : Color.secondary.opacity(0.3),
                                  lineWidth: isActive ? 2 : 1)
            )
            .contentShape(RoundedRectangle(cornerRadius: 10))
        }
        .buttonStyle(.plain)
    }
}
