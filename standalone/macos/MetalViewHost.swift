// license:GPLv3+

// MetalViewHost.swift — embeds a Metal-renderable NSView inside SwiftUI.
//
// The Apple-idiomatic bridge for hosting a custom AppKit view inside a
// SwiftUI hierarchy is `NSViewRepresentable`. It owns the create/update/
// recycle lifecycle of the underlying NSView so SwiftUI's diffing can
// reason about it as if it were a native Swift view.
//
// We use a plain `NSView` with a `CAMetalLayer` backing rather than
// `MTKView`. MTKView wants to drive its own render loop and own drawable
// acquisition via `currentDrawable`; BGFX also calls `[layer nextDrawable]`
// itself, and two consumers of one layer's drawable pool is undefined
// behaviour. The "external renderer owns drawables" pattern documented by
// Apple is exactly NSView + CAMetalLayer.

import SwiftUI
import AppKit
import Metal
import QuartzCore


struct MetalViewHost: NSViewRepresentable {
    /// Fired once when the layer is mounted and has a non-zero drawable
    /// size. The C++ side can safely receive the layer at this point.
    let onLayerReady: (CAMetalLayer) -> Void

    // SwiftUI calls these on the main thread:
    func makeNSView(context: Context) -> MetalNSView {
        let view = MetalNSView()
        view.coordinator = context.coordinator
        return view
    }

    func updateNSView(_ nsView: MetalNSView, context: Context) {
        // No reactive state to apply yet.
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(onLayerReady: onLayerReady)
    }

    // SwiftUI uses Coordinators for the imperative-style callbacks that
    // would normally come from an AppKit delegate.
    final class Coordinator {
        private let onLayerReady: (CAMetalLayer) -> Void
        private var fired = false

        init(onLayerReady: @escaping (CAMetalLayer) -> Void) {
            self.onLayerReady = onLayerReady
        }

        // Idempotent — only the first ready signal propagates.
        func notifyLayerReady(_ layer: CAMetalLayer) {
            guard !fired else { return }
            fired = true
            onLayerReady(layer)
        }
    }
}


/// NSView whose backing layer is a CAMetalLayer. Reports the layer up to
/// its SwiftUI coordinator the first time the view is in a window AND has
/// a non-zero drawable size — at which point a Metal renderer can safely
/// call `[layer nextDrawable]`.
final class MetalNSView: NSView {
    weak var coordinator: MetalViewHost.Coordinator?

    private let metalLayer = CAMetalLayer()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        configureLayer()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configureLayer()
    }

    // The canonical "layer-hosting" pattern: override makeBackingLayer to
    // return our pre-configured CAMetalLayer, then set wantsLayer=true.
    // AppKit calls makeBackingLayer to obtain the view's backing layer;
    // returning our instance makes this view layer-HOSTING (the layer is
    // autonomous and not driven by AppKit's drawing) rather than
    // layer-BACKED (which AppKit fills with its own drawing). Externally-
    // rendered Metal content needs layer-hosting.
    override func makeBackingLayer() -> CALayer {
        return metalLayer
    }

    private func configureLayer() {
        metalLayer.device = MTLCreateSystemDefaultDevice()
        metalLayer.pixelFormat = .bgra8Unorm
        // BGFX may read back from drawables for some post effects.
        metalLayer.framebufferOnly = false

        // Triggers AppKit to call makeBackingLayer above. Order matters
        // — set wantsLayer LAST so the override is visible.
        wantsLayer = true
    }

    override func layout() {
        super.layout()
        updateDrawableSize()
        notifyIfReady()
    }

    private func updateDrawableSize() {
        // Drawable size is in *physical pixels*; multiply view bounds
        // (logical points) by the window's backing scale (typically 2x
        // on Retina). Without this, the layer would render at half
        // resolution on HiDPI displays.
        let scale = window?.backingScaleFactor ?? 1
        let newSize = CGSize(
            width: bounds.width * scale,
            height: bounds.height * scale
        )
        if newSize.width > 0 && newSize.height > 0 && newSize != metalLayer.drawableSize {
            metalLayer.drawableSize = newSize
        }
        // Keep contentsScale in sync with the display so Core Animation
        // doesn't downsample our texture.
        if metalLayer.contentsScale != scale {
            metalLayer.contentsScale = scale
        }
    }

    private func notifyIfReady() {
        guard let window,
              metalLayer.drawableSize.width > 0,
              metalLayer.drawableSize.height > 0
        else { return }
        // Hand the NSWindow to the C side. SDL will adopt it instead
        // of creating its own hidden placeholder. Set BEFORE
        // notifyLayerReady because the launcher fires vpx_run synchronously
        // from the layer callback, and vpx_run will reach the SDL window
        // creation site before returning.
        let ptr = UnsafeMutableRawPointer(Unmanaged.passUnretained(window).toOpaque())
        vpx_set_playfield_nswindow(ptr)
        coordinator?.notifyLayerReady(metalLayer)
    }

    // MARK: - Mouse input

    // AppKit delivers mouse events to the view that hit-tests under the
    // cursor. Buttons and drags come for free; mouseMoved (no button
    // pressed) requires either a window-wide acceptsMouseMovedEvents
    // flag or a per-view tracking area. We use a tracking area so the
    // delivery is scoped to this view — cleaner than mutating window
    // state we don't own (SwiftUI does).

    private var trackingArea: NSTrackingArea?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        updateDrawableSize()
        notifyIfReady()
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let existing = trackingArea {
            removeTrackingArea(existing)
        }
        // .inVisibleRect makes AppKit recompute the rect on every layout
        // pass (so .zero is fine here — it's ignored). .activeInKeyWindow
        // delivers events only while the window has key focus, matching
        // SDL's normal behaviour.
        let area = NSTrackingArea(
            rect: .zero,
            options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
        trackingArea = area
    }

    // Click-through: accept the first click that activates our window
    // without requiring a second click after focus. acceptsFirstResponder
    // lets us be the first responder if anything in the stack tries to
    // set us as such (the keyboard NSEvent monitor doesn't need this,
    // but it's the conventional pair).
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func mouseDown(with event: NSEvent)          { InputForwarder.forwardMouseButton(event, in: self) }
    override func mouseUp(with event: NSEvent)            { InputForwarder.forwardMouseButton(event, in: self) }
    override func mouseDragged(with event: NSEvent)       { InputForwarder.forwardMouseMotion(event, in: self) }
    override func rightMouseDown(with event: NSEvent)     { InputForwarder.forwardMouseButton(event, in: self) }
    override func rightMouseUp(with event: NSEvent)       { InputForwarder.forwardMouseButton(event, in: self) }
    override func rightMouseDragged(with event: NSEvent)  { InputForwarder.forwardMouseMotion(event, in: self) }
    override func otherMouseDown(with event: NSEvent)     { InputForwarder.forwardMouseButton(event, in: self) }
    override func otherMouseUp(with event: NSEvent)       { InputForwarder.forwardMouseButton(event, in: self) }
    override func otherMouseDragged(with event: NSEvent)  { InputForwarder.forwardMouseMotion(event, in: self) }
    override func mouseMoved(with event: NSEvent)         { InputForwarder.forwardMouseMotion(event, in: self) }
    override func scrollWheel(with event: NSEvent)        { InputForwarder.forwardScroll(event) }
}
