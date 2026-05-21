// license:GPLv3+

// InputForwarder.swift — translate macOS NSEvents into SDL input events
// on the SDL queue.
//
// SDL3 normally captures input for windows it created itself. With the
// SwiftUI shell, the user-visible window belongs to SwiftUI; SDL's own
// window is a hidden placeholder for bookkeeping. We bridge the gap on
// the AppKit side and forward events into SDL via vpx_push_*_event
// (which call SDL_PushEvent on the C side). The game's existing
// SDL_PollEvent loop in Player::ProcessOSMessages drains them unchanged.
//
// Two delivery patterns are in use, picked per input class:
//
//   • Keyboard: an NSEvent local monitor. Keys have no spatial identity
//     — they belong to whichever view is first responder — so a global
//     monitor reads cleanly. "Local" means events still propagate to
//     AppKit's responder chain (we return the event), so menu
//     shortcuts and Cmd-Q continue to work.
//
//   • Mouse / scroll: AppKit view overrides on MetalNSView itself.
//     Mouse events are spatial — AppKit's hit testing already routes
//     them to the right view — so the view receives the event, does
//     its own coordinate conversion, and calls into the helpers
//     below. See MetalViewHost.swift for the view-side overrides.

import AppKit


enum InputForwarder {
    /// One-time installation of the keyboard monitor. Idempotent — the
    /// monitor reference is retained by AppKit so we don't need to
    /// hold it ourselves. Mouse forwarding is wired up separately
    /// inside MetalNSView's responder overrides.
    static func install() {
        guard !installed else { return }
        installed = true

        NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp, .flagsChanged]) { event in
            let consumed = forwardKey(event)
            // If we forwarded the key into SDL we should also consume the
            // event so AppKit doesn't dispatch it further. Otherwise the
            // event walks the responder chain, finds nothing to handle
            // it, and the system beeps. Exception: Cmd-modified keys
            // are menu equivalents (Cmd-Q etc.) — let those through.
            if consumed && !event.modifierFlags.contains(.command) {
                return nil
            }
            return event
        }
    }

    private static var installed = false

    /// Returns true if the event was translated to an SDL event and
    /// pushed onto the queue. The caller uses this to decide whether
    /// to consume the NSEvent (preventing AppKit's "unhandled key"
    /// beep) or let it propagate further down the responder chain.
    @discardableResult
    private static func forwardKey(_ event: NSEvent) -> Bool {
        guard let scancode = scancode(forVirtualKey: event.keyCode) else { return false }

        switch event.type {
        case .keyDown:
            vpx_push_key_event(1, scancode)
            return true
        case .keyUp:
            vpx_push_key_event(0, scancode)
            return true
        case .flagsChanged:
            // NSEvent doesn't emit keyDown/keyUp for modifier keys —
            // it emits flagsChanged. Derive press state from the
            // modifier mask. We only care about the modifiers the
            // game actually binds to (shift for flippers, etc.).
            if let isDown = modifierStateChange(for: event, scancode: scancode) {
                vpx_push_key_event(isDown ? 1 : 0, scancode)
                return true
            }
            return false
        default:
            return false
        }
    }

    /// Was the modifier whose scancode is `scancode` just pressed (true)
    /// or just released (false)? Returns nil if we can't tell — e.g.
    /// flagsChanged fired for a modifier we don't care about.
    ///
    /// AppKit's `.shift` / `.control` / `.option` / `.command` masks
    /// don't distinguish left from right — they're set if *either* side
    /// is down. That breaks the common pinball case of holding one
    /// flipper (a shift key) and tapping the other: when the first key
    /// is released, the device-independent mask is still set, so we'd
    /// push another keyDown and the flipper would appear stuck.
    /// `modifierFlags.rawValue` *does* carry per-side bits (the
    /// `NX_DEVICE…KEYMASK` family from `<IOKit/hidsystem/IOLLEvent.h>`).
    /// Read those directly.
    private static func modifierStateChange(for event: NSEvent, scancode: UInt16) -> Bool? {
        let raw = event.modifierFlags.rawValue
        if let mask = deviceModifierMask(for: scancode) {
            return (raw & mask) != 0
        }
        return nil
    }

    /// Per-side modifier bits set in `NSEvent.modifierFlags.rawValue`.
    /// Values from `<IOKit/hidsystem/IOLLEvent.h>` — API-stable since
    /// 10.0. Returning nil means "we don't care about this scancode".
    private static func deviceModifierMask(for scancode: UInt16) -> UInt? {
        switch scancode {
        case SDL_SCANCODE_LSHIFT: return 0x00000002 // NX_DEVICELSHIFTKEYMASK
        case SDL_SCANCODE_RSHIFT: return 0x00000004 // NX_DEVICERSHIFTKEYMASK
        case SDL_SCANCODE_LCTRL:  return 0x00000001 // NX_DEVICELCTLKEYMASK
        case SDL_SCANCODE_RCTRL:  return 0x00002000 // NX_DEVICERCTLKEYMASK
        case SDL_SCANCODE_LALT:   return 0x00000020 // NX_DEVICELALTKEYMASK
        case SDL_SCANCODE_RALT:   return 0x00000040 // NX_DEVICERALTKEYMASK
        case SDL_SCANCODE_LGUI:   return 0x00000008 // NX_DEVICELCMDKEYMASK
        case SDL_SCANCODE_RGUI:   return 0x00000010 // NX_DEVICERCMDKEYMASK
        default: return nil
        }
    }

    /// macOS virtual key code (Carbon kVK_*) → SDL_Scancode.
    /// Only the keys VPX actually binds for default tables are included —
    /// add more here as needed. A full table would be ~120 entries; we
    /// don't need them for a working pinball table.
    private static func scancode(forVirtualKey keyCode: UInt16) -> UInt16? {
        switch Int(keyCode) {
        // Modifier keys (flippers + service-menu chording)
        case 56:  return SDL_SCANCODE_LSHIFT   // kVK_Shift
        case 60:  return SDL_SCANCODE_RSHIFT   // kVK_RightShift
        case 59:  return SDL_SCANCODE_LCTRL    // kVK_Control
        case 62:  return SDL_SCANCODE_RCTRL    // kVK_RightControl
        case 58:  return SDL_SCANCODE_LALT     // kVK_Option
        case 61:  return SDL_SCANCODE_RALT     // kVK_RightOption
        case 55:  return SDL_SCANCODE_LGUI     // kVK_Command
        case 54:  return SDL_SCANCODE_RGUI     // kVK_RightCommand

        // Common game controls
        case 49:  return SDL_SCANCODE_SPACE    // kVK_Space
        case 36:  return SDL_SCANCODE_RETURN   // kVK_Return
        case 53:  return SDL_SCANCODE_ESCAPE   // kVK_Escape
        case 51:  return SDL_SCANCODE_BACKSPACE
        case 48:  return SDL_SCANCODE_TAB

        // Arrows (service menu navigation)
        case 123: return SDL_SCANCODE_LEFT
        case 124: return SDL_SCANCODE_RIGHT
        case 125: return SDL_SCANCODE_DOWN
        case 126: return SDL_SCANCODE_UP

        // Function row (F1..F12 — often bound to debug/help)
        case 122: return SDL_SCANCODE_F1
        case 120: return SDL_SCANCODE_F2
        case 99:  return SDL_SCANCODE_F3
        case 118: return SDL_SCANCODE_F4
        case 96:  return SDL_SCANCODE_F5
        case 97:  return SDL_SCANCODE_F6
        case 98:  return SDL_SCANCODE_F7
        case 100: return SDL_SCANCODE_F8
        case 101: return SDL_SCANCODE_F9
        case 109: return SDL_SCANCODE_F10
        case 103: return SDL_SCANCODE_F11
        case 111: return SDL_SCANCODE_F12

        // LiveUI / common punctuation
        case 50:  return SDL_SCANCODE_GRAVE     // ` (backtick — LiveUI default)
        case 27:  return SDL_SCANCODE_MINUS
        case 24:  return SDL_SCANCODE_EQUALS
        case 33:  return SDL_SCANCODE_LEFTBRACKET
        case 30:  return SDL_SCANCODE_RIGHTBRACKET
        case 42:  return SDL_SCANCODE_BACKSLASH
        case 41:  return SDL_SCANCODE_SEMICOLON
        case 39:  return SDL_SCANCODE_APOSTROPHE
        case 43:  return SDL_SCANCODE_COMMA
        case 47:  return SDL_SCANCODE_PERIOD
        case 44:  return SDL_SCANCODE_SLASH

        // A-Z, 0-9 — kVK_ANSI_* values
        case 0:   return SDL_SCANCODE_A
        case 11:  return SDL_SCANCODE_B
        case 8:   return SDL_SCANCODE_C
        case 2:   return SDL_SCANCODE_D
        case 14:  return SDL_SCANCODE_E
        case 3:   return SDL_SCANCODE_F
        case 5:   return SDL_SCANCODE_G
        case 4:   return SDL_SCANCODE_H
        case 34:  return SDL_SCANCODE_I
        case 38:  return SDL_SCANCODE_J
        case 40:  return SDL_SCANCODE_K
        case 37:  return SDL_SCANCODE_L
        case 46:  return SDL_SCANCODE_M
        case 45:  return SDL_SCANCODE_N
        case 31:  return SDL_SCANCODE_O
        case 35:  return SDL_SCANCODE_P
        case 12:  return SDL_SCANCODE_Q
        case 15:  return SDL_SCANCODE_R
        case 1:   return SDL_SCANCODE_S
        case 17:  return SDL_SCANCODE_T
        case 32:  return SDL_SCANCODE_U
        case 9:   return SDL_SCANCODE_V
        case 13:  return SDL_SCANCODE_W
        case 7:   return SDL_SCANCODE_X
        case 16:  return SDL_SCANCODE_Y
        case 6:   return SDL_SCANCODE_Z
        case 29:  return SDL_SCANCODE_0
        case 18:  return SDL_SCANCODE_1
        case 19:  return SDL_SCANCODE_2
        case 20:  return SDL_SCANCODE_3
        case 21:  return SDL_SCANCODE_4
        case 23:  return SDL_SCANCODE_5
        case 22:  return SDL_SCANCODE_6
        case 26:  return SDL_SCANCODE_7
        case 28:  return SDL_SCANCODE_8
        case 25:  return SDL_SCANCODE_9

        default:  return nil
        }
    }

    // MARK: - Mouse forwarding (called from MetalNSView)

    /// Forward a mouse button event from an AppKit responder. `view`
    /// is the view that received the event (used to translate window
    /// coordinates into view-local pixel coordinates the game expects).
    static func forwardMouseButton(_ event: NSEvent, in view: NSView) {
        guard let button = sdlButton(for: event) else { return }
        let isDown = event.type == .leftMouseDown
            || event.type == .rightMouseDown
            || event.type == .otherMouseDown
        let (x, y) = pointLocation(of: event, in: view)
        vpx_push_mouse_button(isDown ? 1 : 0, Int32(button), Float(x), Float(y))
    }

    /// Forward a mouse motion event (including drag events, which AppKit
    /// delivers as separate types but the game treats as motion).
    static func forwardMouseMotion(_ event: NSEvent, in view: NSView) {
        let (x, y) = pointLocation(of: event, in: view)
        // NSEvent.deltaY uses Cocoa's bottom-up sign; flip to match
        // the top-down x/y we just produced.
        vpx_push_mouse_motion(Float(x), Float(y), Float(event.deltaX), Float(-event.deltaY))
    }

    /// Forward a scroll wheel event.
    static func forwardScroll(_ event: NSEvent) {
        // scrollingDeltaX/Y handles both mouse wheels (line units) and
        // trackpad continuous scrolling (pixel-ish units, normalised by
        // AppKit). Pass through as-is; SDL consumers treat the value as
        // an opaque scroll magnitude.
        vpx_push_mouse_wheel(Float(event.scrollingDeltaX), Float(event.scrollingDeltaY))
    }

    /// Translate an NSEvent's locationInWindow into the playfield's
    /// top-down POINT coordinate space (not pixels — ImGui uses
    /// DisplaySize in points with a separate FramebufferScale for
    /// HiDPI, and giving it pixels here would place the cursor at
    /// 2× the intended position on Retina).
    ///
    /// Two steps:
    ///   1. convert(_:from:nil) → view-local points (still bottom-up)
    ///   2. flip Y against the view's height for top-left origin
    private static func pointLocation(of event: NSEvent, in view: NSView) -> (x: CGFloat, y: CGFloat) {
        let local = view.convert(event.locationInWindow, from: nil)
        return (local.x, view.bounds.height - local.y)
    }

    /// Map an NSEvent's mouse-button type onto SDL's button numbering:
    /// 1 = left, 2 = middle, 3 = right. Returns nil for events we
    /// don't care about.
    private static func sdlButton(for event: NSEvent) -> Int? {
        switch event.type {
        case .leftMouseDown, .leftMouseUp:
            return 1
        case .rightMouseDown, .rightMouseUp:
            return 3
        case .otherMouseDown, .otherMouseUp:
            // NSEvent.buttonNumber: 0=left, 1=right, 2=middle, then 3+
            // for extra buttons. We've already handled left/right via
            // the dedicated types above; "other" starts at the middle
            // button. Translate to SDL's 2=middle, 4=back, 5=forward.
            switch event.buttonNumber {
            case 2:  return 2  // middle
            case 3:  return 4  // back
            case 4:  return 5  // forward
            default: return event.buttonNumber + 1
            }
        default:
            return nil
        }
    }
}


/// Lift SDL_Scancode raw values into Swift-typed UInt16 constants so the
/// switch above doesn't have to call SDL through a generated import for
/// every comparison. Mirrors SDL_scancode.h. Update if SDL ever
/// renumbers them (unlikely — they're API-stable).
private let SDL_SCANCODE_RETURN:    UInt16 = 40
private let SDL_SCANCODE_ESCAPE:    UInt16 = 41
private let SDL_SCANCODE_BACKSPACE: UInt16 = 42
private let SDL_SCANCODE_TAB:       UInt16 = 43
private let SDL_SCANCODE_SPACE:     UInt16 = 44

private let SDL_SCANCODE_MINUS:        UInt16 = 45
private let SDL_SCANCODE_EQUALS:       UInt16 = 46
private let SDL_SCANCODE_LEFTBRACKET:  UInt16 = 47
private let SDL_SCANCODE_RIGHTBRACKET: UInt16 = 48
private let SDL_SCANCODE_BACKSLASH:    UInt16 = 49
private let SDL_SCANCODE_SEMICOLON:    UInt16 = 51
private let SDL_SCANCODE_APOSTROPHE:   UInt16 = 52
private let SDL_SCANCODE_GRAVE:        UInt16 = 53
private let SDL_SCANCODE_COMMA:        UInt16 = 54
private let SDL_SCANCODE_PERIOD:       UInt16 = 55
private let SDL_SCANCODE_SLASH:        UInt16 = 56

private let SDL_SCANCODE_A: UInt16 = 4
private let SDL_SCANCODE_B: UInt16 = 5
private let SDL_SCANCODE_C: UInt16 = 6
private let SDL_SCANCODE_D: UInt16 = 7
private let SDL_SCANCODE_E: UInt16 = 8
private let SDL_SCANCODE_F: UInt16 = 9
private let SDL_SCANCODE_G: UInt16 = 10
private let SDL_SCANCODE_H: UInt16 = 11
private let SDL_SCANCODE_I: UInt16 = 12
private let SDL_SCANCODE_J: UInt16 = 13
private let SDL_SCANCODE_K: UInt16 = 14
private let SDL_SCANCODE_L: UInt16 = 15
private let SDL_SCANCODE_M: UInt16 = 16
private let SDL_SCANCODE_N: UInt16 = 17
private let SDL_SCANCODE_O: UInt16 = 18
private let SDL_SCANCODE_P: UInt16 = 19
private let SDL_SCANCODE_Q: UInt16 = 20
private let SDL_SCANCODE_R: UInt16 = 21
private let SDL_SCANCODE_S: UInt16 = 22
private let SDL_SCANCODE_T: UInt16 = 23
private let SDL_SCANCODE_U: UInt16 = 24
private let SDL_SCANCODE_V: UInt16 = 25
private let SDL_SCANCODE_W: UInt16 = 26
private let SDL_SCANCODE_X: UInt16 = 27
private let SDL_SCANCODE_Y: UInt16 = 28
private let SDL_SCANCODE_Z: UInt16 = 29

private let SDL_SCANCODE_1: UInt16 = 30
private let SDL_SCANCODE_2: UInt16 = 31
private let SDL_SCANCODE_3: UInt16 = 32
private let SDL_SCANCODE_4: UInt16 = 33
private let SDL_SCANCODE_5: UInt16 = 34
private let SDL_SCANCODE_6: UInt16 = 35
private let SDL_SCANCODE_7: UInt16 = 36
private let SDL_SCANCODE_8: UInt16 = 37
private let SDL_SCANCODE_9: UInt16 = 38
private let SDL_SCANCODE_0: UInt16 = 39

private let SDL_SCANCODE_RIGHT: UInt16 = 79
private let SDL_SCANCODE_LEFT:  UInt16 = 80
private let SDL_SCANCODE_DOWN:  UInt16 = 81
private let SDL_SCANCODE_UP:    UInt16 = 82

private let SDL_SCANCODE_F1:  UInt16 = 58
private let SDL_SCANCODE_F2:  UInt16 = 59
private let SDL_SCANCODE_F3:  UInt16 = 60
private let SDL_SCANCODE_F4:  UInt16 = 61
private let SDL_SCANCODE_F5:  UInt16 = 62
private let SDL_SCANCODE_F6:  UInt16 = 63
private let SDL_SCANCODE_F7:  UInt16 = 64
private let SDL_SCANCODE_F8:  UInt16 = 65
private let SDL_SCANCODE_F9:  UInt16 = 66
private let SDL_SCANCODE_F10: UInt16 = 67
private let SDL_SCANCODE_F11: UInt16 = 68
private let SDL_SCANCODE_F12: UInt16 = 69

private let SDL_SCANCODE_LCTRL:  UInt16 = 224
private let SDL_SCANCODE_LSHIFT: UInt16 = 225
private let SDL_SCANCODE_LALT:   UInt16 = 226
private let SDL_SCANCODE_LGUI:   UInt16 = 227
private let SDL_SCANCODE_RCTRL:  UInt16 = 228
private let SDL_SCANCODE_RSHIFT: UInt16 = 229
private let SDL_SCANCODE_RALT:   UInt16 = 230
private let SDL_SCANCODE_RGUI:   UInt16 = 231
