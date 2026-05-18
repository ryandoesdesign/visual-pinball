// license:GPLv3+
//
// DisplaySettingsView.swift — SwiftUI port of the C++ Display Settings
// page (src/ui/live/ingameui/DisplaySettingsPage.cpp). Live preview
// only — no persistence this slice.
//
// Reads/writes the engine through the C ABI in SettingsBridge.h.
// All bridge calls are safe from the main thread:
//   - getters return current state synchronously
//   - setters queue a lambda for the render thread via
//     AddEndOfFrameCmd, so the actual window mutation runs in a
//     thread-safe spot
//
// SwiftUI polls the bridge every 250ms so external state changes
// (drag-to-resize the playfield window, etc.) propagate back into the
// settings UI.

import SwiftUI
import Foundation


// Mirrors the aspect-ratio table in DisplaySettingsPage.cpp:23. Index
// 0 = Free (no constraint); >0 enforces the (x, y) ratio. Kept in
// Swift rather than exposed via the bridge so the lookup is local.
private struct AspectRatio: Hashable {
    let x: Int
    let y: Int
    let label: String
}

private let aspectRatios: [AspectRatio] = [
    .init(x: 0,  y: 0,  label: "Free"),
    .init(x: 4,  y: 3,  label: "Landscape — 4:3"),
    .init(x: 16, y: 10, label: "Landscape — 16:10"),
    .init(x: 16, y: 9,  label: "Landscape — 16:9"),
    .init(x: 21, y: 10, label: "Landscape — 21:10"),
    .init(x: 21, y: 9,  label: "Landscape — 21:9"),
    .init(x: 4,  y: 1,  label: "Landscape — 4:1 (DMD)"),
    .init(x: 3,  y: 4,  label: "Portrait — 3:4"),
    .init(x: 10, y: 16, label: "Portrait — 10:16"),
    .init(x: 9,  y: 16, label: "Portrait — 9:16"),
    .init(x: 10, y: 21, label: "Portrait — 10:21"),
    .init(x: 9,  y: 21, label: "Portrait — 9:21"),
    .init(x: 1,  y: 4,  label: "Portrait — 1:4 (DMD)"),
]


private struct DisplayInfo: Identifiable, Hashable {
    let id: Int
    let name: String
    let width: Int
    let height: Int
    let isPrimary: Bool
}


// State holder for the Display Settings tab. ObservableObject + Timer
// polling gives us a one-way data binding for free: any state change
// originating in the engine (drag-resize, fullscreen toggle, etc.)
// becomes visible in the UI within ~250ms without needing a
// C-to-Swift callback infrastructure.
private final class DisplaySettingsModel: ObservableObject {
    @Published var isActive: Bool = false
    @Published var displays: [DisplayInfo] = []
    @Published var selectedDisplay: Int = 0
    @Published var arLock: Int = 0
    @Published var width: Int = 0
    @Published var height: Int = 0
    @Published var posX: Int = 0
    @Published var posY: Int = 0

    private var timer: Timer?

    init() {
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            self?.refresh()
        }
    }

    deinit {
        timer?.invalidate()
    }

    func refresh() {
        let active = vpx_settings_is_player_active() != 0
        isActive = active
        guard active else { return }

        // Displays — query each refresh because the user may plug in
        // or unplug a monitor while Settings is open.
        var buf = [vpx_display_info_t](
            repeating: vpx_display_info_t(index: 0, width: 0, height: 0, is_primary: 0,
                                          name: (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)),
            count: 8)
        let n = Int(vpx_settings_get_displays(&buf, Int32(buf.count)))
        displays = (0..<n).map { i in
            // Pull the name string by copying out of the C char array.
            // Swift strings need the contiguous-byte form, which we
            // get via withUnsafeBytes on the tuple.
            var localBuf = buf[i].name
            let name = withUnsafePointer(to: &localBuf) { ptr -> String in
                ptr.withMemoryRebound(to: CChar.self, capacity: 128) {
                    String(cString: $0)
                }
            }
            return DisplayInfo(
                id: Int(buf[i].index),
                name: name,
                width: Int(buf[i].width),
                height: Int(buf[i].height),
                isPrimary: buf[i].is_primary != 0
            )
        }

        // Window state
        var w: Int32 = 0, h: Int32 = 0
        if vpx_settings_get_window_size(VPX_WND_PLAYFIELD, &w, &h) != 0 {
            width = Int(w)
            height = Int(h)
        }
        var x: Int32 = 0, y: Int32 = 0
        if vpx_settings_get_window_position(VPX_WND_PLAYFIELD, &x, &y) != 0 {
            posX = Int(x)
            posY = Int(y)
        }
        let dispIdx = Int(vpx_settings_get_window_display(VPX_WND_PLAYFIELD))
        if dispIdx >= 0 { selectedDisplay = dispIdx }
        arLock = Int(vpx_settings_get_window_arlock(VPX_WND_PLAYFIELD))
    }

    // --- Mutators (queued to render thread by the bridge) ---

    func setDisplay(_ i: Int) {
        selectedDisplay = i
        vpx_settings_set_window_display(VPX_WND_PLAYFIELD, Int32(i))
    }

    func setArLock(_ i: Int) {
        arLock = i
        vpx_settings_set_window_arlock(VPX_WND_PLAYFIELD, Int32(i))
    }

    // Apply AR constraint to a proposed size, mirroring the logic at
    // DisplaySettingsPage.cpp:447-456.
    private func constrainedFromWidth(_ newW: Int) -> (Int, Int) {
        guard arLock > 0, arLock < aspectRatios.count else { return (newW, height) }
        let ar = aspectRatios[arLock]
        return (newW, (newW * ar.y) / ar.x)
    }
    private func constrainedFromHeight(_ newH: Int) -> (Int, Int) {
        guard arLock > 0, arLock < aspectRatios.count else { return (width, newH) }
        let ar = aspectRatios[arLock]
        return ((newH * ar.x) / ar.y, newH)
    }

    func commitWidth(_ newW: Int) {
        let (w, h) = constrainedFromWidth(newW)
        width = w; height = h
        vpx_settings_set_window_size(VPX_WND_PLAYFIELD, Int32(w), Int32(h))
    }

    func commitHeight(_ newH: Int) {
        let (w, h) = constrainedFromHeight(newH)
        width = w; height = h
        vpx_settings_set_window_size(VPX_WND_PLAYFIELD, Int32(w), Int32(h))
    }

    func commitPosX(_ newX: Int) {
        posX = newX
        vpx_settings_set_window_position(VPX_WND_PLAYFIELD, Int32(newX), Int32(posY))
    }

    func commitPosY(_ newY: Int) {
        posY = newY
        vpx_settings_set_window_position(VPX_WND_PLAYFIELD, Int32(posX), Int32(newY))
    }
}


struct DisplaySettingsView: View {
    @StateObject private var model = DisplaySettingsModel()

    var body: some View {
        Form {
            if !model.isActive {
                Text("Load a table to edit display settings.")
                    .foregroundStyle(.secondary)
            } else {
                playfieldSection
            }
        }
        .formStyle(.grouped)
        .disabled(!model.isActive)
        .padding()
    }

    private var playfieldSection: some View {
        Section("Playfield Window") {
            Picker("Display", selection: Binding(
                get: { model.selectedDisplay },
                set: { model.setDisplay($0) }
            )) {
                ForEach(model.displays) { d in
                    Text("\(d.isPrimary ? "★ " : "")\(d.name) — \(d.width)×\(d.height)")
                        .tag(d.id)
                }
            }

            Picker("Aspect ratio", selection: Binding(
                get: { model.arLock },
                set: { model.setArLock($0) }
            )) {
                ForEach(0..<aspectRatios.count, id: \.self) { i in
                    Text(aspectRatios[i].label).tag(i)
                }
            }

            // Steppers commit on each click (one bridge call per
            // change), keeping the queued-command rate low.
            Stepper(value: Binding(
                get: { model.width },
                set: { model.commitWidth($0) }
            ), in: 320...8192, step: 10) {
                LabeledContent("Width", value: "\(model.width) px")
            }

            Stepper(value: Binding(
                get: { model.height },
                set: { model.commitHeight($0) }
            ), in: 320...8192, step: 10) {
                LabeledContent("Height", value: "\(model.height) px")
            }

            Stepper(value: Binding(
                get: { model.posX },
                set: { model.commitPosX($0) }
            ), in: 0...8192, step: 10) {
                LabeledContent("Position X", value: "\(model.posX) px")
            }

            Stepper(value: Binding(
                get: { model.posY },
                set: { model.commitPosY($0) }
            ), in: 0...8192, step: 10) {
                LabeledContent("Position Y", value: "\(model.posY) px")
            }
        }
    }
}
