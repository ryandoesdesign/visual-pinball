// license:GPLv3+

// VPXApp.swift — the Swift entry point.
//
// `@main` makes the Swift compiler synthesise the C `main` symbol from
// this type's `App.main()`. The compiled binary's `main` is provided by
// Swift; the C++ side is reached downstream via the CBridge.

import SwiftUI
import AppKit
import UniformTypeIdentifiers


@main
struct VPXApp: App {
    // SwiftUI's App protocol doesn't expose every classic
    // NSApplicationDelegate hook. `@NSApplicationDelegateAdaptor`
    // creates and registers a delegate so applicationDidFinishLaunching
    // still fires — the bridge between SwiftUI's declarative lifecycle
    // and AppKit's imperative one.
    @NSApplicationDelegateAdaptor(VPXAppDelegate.self) var delegate

    // `@StateObject` is SwiftUI's "owned reference type" — created once
    // when the App instance first runs and held for its lifetime, with
    // changes to its @Published properties triggering view updates.
    @StateObject private var pickerState = PickerState()

    var body: some Scene {
        WindowGroup("VPinballX") {
            MetalViewHost { layer in
                // Layer is mounted and has a non-zero drawable size.
                // Hand it to the launcher; it'll trigger vpx_run once
                // the args (from CLI or file picker) have also resolved.
                VPXLauncher.shared.setLayer(layer)
            }
            .overlay(alignment: .bottom) {
                // Translucent reminder of the primary key bindings;
                // appears at table load, fades out after ~6 seconds.
                ControlsHintView()
                    .padding(.bottom, 32)
            }
            .frame(minWidth: 1280, minHeight: 720)
            .fileImporter(
                isPresented: $pickerState.isShowing,
                allowedContentTypes: vpxContentTypes,
                allowsMultipleSelection: false
            ) { result in
                switch result {
                case .success(let urls):
                    if let url = urls.first {
                        VPXLauncher.shared.setArgs([
                            CommandLine.arguments[0],
                            "-play",
                            url.path,
                        ])
                    } else {
                        // User confirmed with no selection — shouldn't
                        // happen for a single-selection picker, but
                        // handle it the same as cancel.
                        VPXAppDelegate.bailOutNoTable()
                    }
                case .failure:
                    VPXAppDelegate.bailOutNoTable()
                }
            }
            .onAppear {
                // First appearance of the window. Decide whether to
                // show the picker (no CLI args) or launch straight
                // away (table path was supplied).
                if CommandLine.argc == 1 {
                    pickerState.isShowing = true
                } else {
                    let args = (0..<Int(CommandLine.argc)).compactMap { i -> String? in
                        guard let p = CommandLine.unsafeArgv[i] else { return nil }
                        return String(cString: p)
                    }
                    VPXLauncher.shared.setArgs(args)
                }
            }
        }
        .commands {
            // View-menu integration for the controls-hint overlay.
            // CommandGroup(after: .toolbar) inserts after the system's
            // Show/Hide Toolbar item in the auto-generated View menu.
            // Cmd+I matches the macOS "Info" convention; SwiftUI menu
            // shortcuts intercept the keypress before it reaches the
            // playfield's input forwarding, so this is the only place
            // we need to wire it.
            CommandGroup(after: .toolbar) {
                Button(hintModel.isVisible ? "Hide Controls Hint" : "Show Controls Hint") {
                    hintModel.toggle()
                }
                .keyboardShortcut("i", modifiers: .command)
            }
        }

        // Cmd+, opens this scene automatically — SwiftUI wires the
        // standard "<app> ▸ Settings…" menu item to it. Lives as a
        // separate NSWindow from the playfield; game keeps running
        // behind while it's open. Declared AFTER WindowGroup so the
        // playfield is the primary launch scene.
        Settings {
            SettingsRoot()
        }
    }

    /// Observed so the View-menu button's title can flip between
    /// "Show Controls Hint" and "Hide Controls Hint" reactively.
    @ObservedObject private var hintModel = ControlsHintModel.shared

    /// UTType(filenameExtension:) returns Optional; force-unwrap is OK
    /// here because "vpx" is a fixed, known-good extension.
    private var vpxContentTypes: [UTType] {
        if let t = UTType(filenameExtension: "vpx") {
            return [t]
        }
        return []
    }
}


/// Shared state used by the SwiftUI scene to drive the .fileImporter
/// presentation. ObservableObject + @Published is the standard SwiftUI
/// data-flow primitive: views observing a @Published property re-render
/// when it changes.
final class PickerState: ObservableObject {
    @Published var isShowing = false
}


final class VPXAppDelegate: NSObject, NSApplicationDelegate {
    // SwiftUI lifecycle quirk: with @NSApplicationDelegateAdaptor and
    // the main thread hijacked by vpx_run during MetalNSView setup,
    // applicationDidFinishLaunching never fires. applicationWillFinish
    // and applicationDidBecomeActive both still fire — Active runs
    // when the app is first foregrounded (after the user clicks the
    // window, or immediately if launched via `open`), which is
    // sufficient for our needs (input doesn't matter until the user
    // interacts).
    func applicationDidBecomeActive(_ notification: Notification) {
        // Idempotent — safe to call on every activation.
        InputForwarder.install()

        // Register the default view preset with the engine. The
        // engine applies it during Player construction, so every
        // table load starts with the preset values. Idempotent.
        ViewPresetAutoApplier.install()

        // When launched from a terminal (running the binary directly
        // rather than via `open`), macOS may not auto-activate the
        // process — its window can come up but never become key,
        // which means the window server routes no input events to us.
        // No-op for properly-bundled launches.
        NSApp.activate(ignoringOtherApps: true)
    }

    /// Called from the .fileImporter callback when the user cancels or
    /// fails to pick a table. Show a friendly alert and quit.
    static func bailOutNoTable() {
        let alert = NSAlert()
        alert.messageText = """
            VPinballX

            You must choose a VPX table to start, run using the command line \
            to set arguments, or double click a ".vpx" file.
            """
        alert.addButton(withTitle: "OK")
        alert.alertStyle = .warning
        alert.runModal()
        NSApp.terminate(nil)
    }
}
