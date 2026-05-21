// license:GPLv3+
//
// AudioSettingsView.swift — SwiftUI port of the C++ Audio Settings page
// (src/ui/live/ingameui/AudioSettingsPage.cpp). Live preview only — no
// persistence this slice.
//
// Volumes are 0..100 (matching the engine's PropInt range). The
// "Lock Volumes" toggle cross-couples music and sound on the SwiftUI
// side: when on, dragging one volume drags the other by the same
// delta, clamped to 0..100. Same behaviour as the ImGui page; the
// engine itself doesn't know about the lock.

import SwiftUI
import Foundation


private let sound3dLabels: [String] = [
    "2 Front channels",
    "2 Rear channels",
    "Up to 6 channels — rear at lockbar",
    "Up to 6 channels — front at lockbar",
    "6ch side & rear at lockbar (legacy mix)",
    "6ch side & rear at lockbar (SSF mix)",
]


private struct AudioDeviceInfo: Identifiable, Hashable {
    let id: Int
    let name: String
    let channels: Int
}


private final class AudioSettingsModel: ObservableObject {
    @Published var isActive: Bool = false

    @Published var musicVolume: Int = 0      // backglass
    @Published var soundVolume: Int = 0      // playfield
    @Published var playMusic: Bool = true
    @Published var playSound: Bool = true
    @Published var lockVolumes: Bool = true

    @Published var devices: [AudioDeviceInfo] = []
    @Published var backglassDevice: Int = -1
    @Published var playfieldDevice: Int = -1
    @Published var sound3dMode: Int = 0

    private var timer: Timer?

    init() {
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            self?.refresh()
        }
    }

    deinit { timer?.invalidate() }

    func refresh() {
        let active = vpx_settings_is_player_active() != 0
        isActive = active

        // Devices are enumerated regardless of player state — useful
        // for showing a populated picker before a table loads.
        devices = enumerateDevices()

        guard active else { return }

        musicVolume      = Int(vpx_audio_get_music_volume())
        soundVolume      = Int(vpx_audio_get_sound_volume())
        playMusic        = vpx_audio_get_play_music() != 0
        playSound        = vpx_audio_get_play_sound() != 0
        lockVolumes      = vpx_audio_get_lock_volumes() != 0
        backglassDevice  = Int(vpx_audio_get_backglass_device())
        playfieldDevice  = Int(vpx_audio_get_playfield_device())
        sound3dMode      = Int(vpx_audio_get_sound3d_mode())
    }

    private func enumerateDevices() -> [AudioDeviceInfo] {
        // The C struct's `name` field is a 256-byte char array which
        // Swift imports as a giant tuple type that's awkward to spell.
        // Sidestep the issue by zero-allocating raw bytes and binding
        // them to the C struct type — same trick as a calloc + cast.
        let cap = 16
        let stride = MemoryLayout<vpx_audio_device_t>.stride
        let raw = UnsafeMutableRawPointer.allocate(
            byteCount: stride * cap,
            alignment: MemoryLayout<vpx_audio_device_t>.alignment)
        defer { raw.deallocate() }
        memset(raw, 0, stride * cap)
        let buf = raw.bindMemory(to: vpx_audio_device_t.self, capacity: cap)
        let n = Int(vpx_audio_get_devices(buf, Int32(cap)))

        return (0..<n).map { i in
            // Read the name tuple via a typed pointer (avoids spelling
            // out the 256-element char tuple type by hand).
            var localBuf = buf[i].name
            let name = withUnsafePointer(to: &localBuf) { ptr -> String in
                ptr.withMemoryRebound(to: CChar.self, capacity: 256) {
                    String(cString: $0)
                }
            }
            return AudioDeviceInfo(
                id: Int(buf[i].index),
                name: name,
                channels: Int(buf[i].channels)
            )
        }
    }

    // --- Mutators ---

    private func clamp(_ x: Int) -> Int { min(max(x, 0), 100) }

    func commitMusicVolume(_ v: Int) {
        let new = clamp(v)
        if lockVolumes {
            let delta = new - musicVolume
            soundVolume = clamp(soundVolume + delta)
            vpx_audio_set_sound_volume(Int32(soundVolume))
        }
        musicVolume = new
        vpx_audio_set_music_volume(Int32(new))
    }

    func commitSoundVolume(_ v: Int) {
        let new = clamp(v)
        if lockVolumes {
            let delta = new - soundVolume
            musicVolume = clamp(musicVolume + delta)
            vpx_audio_set_music_volume(Int32(musicVolume))
        }
        soundVolume = new
        vpx_audio_set_sound_volume(Int32(new))
    }

    func commitPlayMusic(_ v: Bool) {
        playMusic = v
        vpx_audio_set_play_music(v ? 1 : 0)
    }

    func commitPlaySound(_ v: Bool) {
        playSound = v
        vpx_audio_set_play_sound(v ? 1 : 0)
    }

    func commitLockVolumes(_ v: Bool) {
        lockVolumes = v
        vpx_audio_set_lock_volumes(v ? 1 : 0)
    }

    func commitBackglassDevice(_ i: Int) {
        backglassDevice = i
        vpx_audio_set_backglass_device(Int32(i))
    }

    func commitPlayfieldDevice(_ i: Int) {
        playfieldDevice = i
        vpx_audio_set_playfield_device(Int32(i))
    }

    func commitSound3dMode(_ i: Int) {
        sound3dMode = i
        vpx_audio_set_sound3d_mode(Int32(i))
    }
}


struct AudioSettingsView: View {
    @StateObject private var model = AudioSettingsModel()

    var body: some View {
        Form {
            if !model.isActive {
                Text("Load a table to edit audio settings.")
                    .foregroundStyle(.secondary)
            } else {
                volumesSection
                routingSection
                devicesSection
            }
        }
        .formStyle(.grouped)
        .disabled(!model.isActive)
        .padding()
    }

    private var volumesSection: some View {
        Section("Volumes") {
            volumeSlider(
                label: "Backglass",
                value: model.musicVolume,
                onCommit: model.commitMusicVolume
            )
            volumeSlider(
                label: "Playfield",
                value: model.soundVolume,
                onCommit: model.commitSoundVolume
            )
            Toggle("Lock volumes", isOn: Binding(
                get: { model.lockVolumes },
                set: { model.commitLockVolumes($0) }
            ))
            .help("When on, dragging either slider drags the other by the same amount.")
        }
    }

    private var routingSection: some View {
        Section("Routing") {
            Toggle("Enable backglass audio", isOn: Binding(
                get: { model.playMusic },
                set: { model.commitPlayMusic($0) }
            ))
            Toggle("Enable playfield audio", isOn: Binding(
                get: { model.playSound },
                set: { model.commitPlaySound($0) }
            ))
        }
    }

    private var devicesSection: some View {
        Section("Devices") {
            Picker("Backglass device", selection: Binding(
                get: { model.backglassDevice },
                set: { model.commitBackglassDevice($0) }
            )) {
                if model.backglassDevice < 0 {
                    Text("Unavailable").tag(-1)
                }
                ForEach(model.devices) { d in
                    Text(deviceLabel(d)).tag(d.id)
                }
            }

            Picker("Playfield device", selection: Binding(
                get: { model.playfieldDevice },
                set: { model.commitPlayfieldDevice($0) }
            )) {
                if model.playfieldDevice < 0 {
                    Text("Unavailable").tag(-1)
                }
                ForEach(model.devices) { d in
                    Text(deviceLabel(d)).tag(d.id)
                }
            }

            Picker("Playfield output mode", selection: Binding(
                get: { model.sound3dMode },
                set: { model.commitSound3dMode($0) }
            )) {
                ForEach(0..<sound3dLabels.count, id: \.self) { i in
                    Text(sound3dLabels[i]).tag(i)
                }
            }
        }
    }

    private func deviceLabel(_ d: AudioDeviceInfo) -> String {
        d.channels > 0 ? "\(d.name) — \(d.channels)ch" : d.name
    }

    private func volumeSlider(label: String, value: Int, onCommit: @escaping (Int) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(label)
                Spacer()
                Text("\(value)%")
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }
            Slider(
                value: Binding(
                    get: { Double(value) },
                    set: { onCommit(Int($0.rounded())) }
                ),
                in: 0...100,
                step: 1
            )
        }
    }
}
