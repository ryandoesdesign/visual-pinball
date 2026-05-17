// license:GPLv3+

// CBridge.h — Swift→C++ bridging header.
//
// Imported into Swift via the -import-objc-header flag (see CMakeLists.txt).
// Swift's importer treats every declaration here as if it lived in a Swift
// module called __ObjC; everything below appears in Swift code with its
// C-style name and an auto-imported Swift signature.
//
// Keep this file strictly C-compatible: no C++ types, no Obj-C imports, no
// templates. Anything fancier than that belongs in CBridge.mm.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Run the full vpinball lifecycle: init → run command → shutdown.
// Blocks until the game loop exits. Returns the process exit code.
//
// Future commits will split this back into individually-exposed init /
// run_command / shutdown C functions when the SwiftUI shell needs to
// interleave (e.g. create the SwiftUI window before run_command starts).
int vpx_run(int argc, char** argv);

// Hand the SwiftUI-owned CAMetalLayer to the C++ side. Non-owning: the
// Swift view retains the layer for the app's lifetime. Must be called
// before vpx_run; the renderer (RenderDevice.cpp) reads it via
// vpx_get_metal_layer() below when initialising BGFX.
//
// `layer` is a `CAMetalLayer*` typed as `void*` to keep this header
// strictly C-compatible (no Obj-C imports).
void  vpx_set_metal_layer(void* layer);
void* vpx_get_metal_layer(void);

// Read the current CAMetalLayer's drawableSize (physical pixels).
// RenderDevice.cpp uses this for init.resolution so BGFX renders at
// the SwiftUI window's real pixel size, not the SDL bookkeeping
// window's size (which would stretch the playfield).
void vpx_get_metal_layer_size(int* outWidth, int* outHeight);

// One game tick. Called from Swift's CADisplayLink callback (registered
// by MetalNSView), which fires at the display's vsync rate. Forwards
// to Player::Tick() — see src/core/main.cpp.
void vpx_tick(void);

// Drain pending NSEvents through NSApp's dispatch path (up to ~1ms).
// Called from Player::GameLoop on macOS in lieu of SDL's busy-sleep,
// so menu bar items, Cmd-Q, window resize, and other AppKit/SwiftUI
// interactions stay responsive while the game is running.
void vpx_pump_runloop_once(void);

// Push a synthesised keyboard event into SDL's queue. Called from the
// Swift NSEvent monitor (see VPXApp.swift) so keypresses on our
// SwiftUI window reach the game's SDL_PollEvent loop without SDL
// needing to own or adopt the window itself.
//
// scancode is an SDL_Scancode value (uint16_t to keep this header C-only);
// isDown is 1 for key-down, 0 for key-up.
void vpx_push_key_event(int isDown, unsigned short scancode);

// Push a synthesised mouse button event. button is 1=left, 2=middle,
// 3=right (the SDL convention). x/y are pixel coordinates in the
// playfield's top-down coordinate space (origin top-left).
void vpx_push_mouse_button(int isDown, int button, float x, float y);

// Push a synthesised mouse motion event. dx/dy are deltas since the
// previous motion (SDL fills xrel/yrel from these).
void vpx_push_mouse_motion(float x, float y, float dx, float dy);

// Push a synthesised scroll wheel event. x/y are scroll deltas (lines or
// fractional lines). Positive y scrolls up.
void vpx_push_mouse_wheel(float x, float y);

// Hand the SwiftUI-owned NSWindow to the C++ side. Called from
// MetalNSView once it knows its window. The pointer is consumed by
// Window.cpp on the macOS shell to back SDL's SDL_Window with the
// SwiftUI NSWindow (via SDL_PROP_WINDOW_CREATE_COCOA_WINDOW_POINTER)
// rather than letting SDL create its own hidden placeholder.
//
// Non-owning: SwiftUI retains the window for the app's lifetime; C
// just holds a borrowed reference. Typed `void*` to keep this header
// strictly C-compatible.
void  vpx_set_playfield_nswindow(void* nsWindow);
void* vpx_get_playfield_nswindow(void);

#ifdef __cplusplus
}
#endif
