// license:GPLv3+

// VPXLauncher.swift — coordinates the launch handoff to the C++ side.
//
// The C++ side needs three things to start running the game:
//   1. The parsed argv (from the file picker or CLI).
//   2. The CAMetalLayer to render into.
//   3. (Future) The NSWindow that owns the layer, for SDL3 to wrap.
//
// These arrive asynchronously and in unpredictable order:
//   - The CLI / file picker path resolves args via NSApplicationDelegate.
//   - The CAMetalLayer becomes valid only after SwiftUI mounts MetalViewHost.
//
// A small "wait for both, then go" coordinator avoids race conditions
// without forcing one side to know about the other.

import AppKit
import QuartzCore


final class VPXLauncher {
    static let shared = VPXLauncher()

    private let lock = NSLock()
    private var args: [String]?
    private var layer: CAMetalLayer?
    private var launched = false

    private init() {}

    func setArgs(_ args: [String]) {
        lock.lock(); defer { lock.unlock() }
        self.args = args
        tryLaunchLocked()
    }

    func setLayer(_ layer: CAMetalLayer) {
        lock.lock(); defer { lock.unlock() }
        self.layer = layer
        tryLaunchLocked()
    }

    private func tryLaunchLocked() {
        guard !launched, let args, let layer else { return }
        launched = true

        // `Unmanaged.passUnretained(...).toOpaque()` hands C an opaque
        // pointer to the layer without transferring ownership. The Swift
        // side (MetalNSView) retains the layer for the app's lifetime;
        // C holds a non-owning reference. The exit() below means we
        // never need to release.
        let layerPtr = UnsafeMutableRawPointer(Unmanaged.passUnretained(layer).toOpaque())
        vpx_set_metal_layer(layerPtr)

        // strdup each arg into C strings, build a C-style argv, and call
        // vpx_run. Note vpx_run blocks until the game loop ends; we call
        // exit() with the result so the strdup'd memory leaks
        // intentionally (process-lifetime).
        var argv: [UnsafeMutablePointer<CChar>?] = args.map { strdup($0) }
        argv.withUnsafeMutableBufferPointer { buf in
            let rc = vpx_run(Int32(args.count), buf.baseAddress)
            exit(rc)
        }
    }
}
