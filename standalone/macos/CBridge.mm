// license:GPLv3+

// CBridge.mm — implementation of the Swift→C++ bridge.
//
// The three-phase split (vpx_init / vpx_run_command / vpx_shutdown) lives
// in src/core/main.cpp. This file orchestrates them for the Swift caller
// and owns small details that naturally belong at the platform boundary
// — the SIGINT handler that main.mm used to install, the runloop pump
// that Player::GameLoop uses on macOS, and the post-game NSApp.terminate
// that quits the SwiftUI shell once the game ends.

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include <SDL3/SDL.h>

#include "CBridge.h"

extern "C" {
   int  vpx_init();
   int  vpx_run_command();
   void vpx_shutdown();

   // Existing globals consumed by VPApp / CommandLineProcessor. Declared
   // extern in src/core/extern.h; we set them here from the Swift-supplied
   // argv before calling vpx_init().
   extern int    g_argc;
   extern char** g_argv;
}


// File-static pointer to the SwiftUI-owned CAMetalLayer. Populated by
// vpx_set_metal_layer (called from Swift's VPXLauncher before vpx_run).
// Commit 4 will plumb this into RenderDevice.cpp; for now we just store
// + log to verify the bridge.
static void* g_metal_layer = nullptr;


static void on_signal(int signum)
{
   printf("Exiting from signal: %d\n", signum);
   exit(-9999);
}


void vpx_set_metal_layer(void* layer)
{
   g_metal_layer = layer;
}


void* vpx_get_metal_layer()
{
   return g_metal_layer;
}


void vpx_get_metal_layer_size(int* outWidth, int* outHeight)
{
   *outWidth = 0;
   *outHeight = 0;
   if (g_metal_layer == nullptr)
      return;
   CAMetalLayer* layer = (__bridge CAMetalLayer*)g_metal_layer;
   CGSize size = layer.drawableSize;
   *outWidth = (int)size.width;
   *outHeight = (int)size.height;
}


// Look up SDL's only window. The game gates per-input handling on the
// windowID matching the playfield window; with no SDL window adoption
// our synthesised events would carry windowID=0 and the game would
// treat them as foreign and drop them. SDL only ever creates one
// (the hidden bookkeeping window — see CBridge's setup), so it's
// effectively the playfield as far as the event filter cares.
static SDL_WindowID GetForwarderWindowID()
{
   SDL_WindowID windowID = 0;
   int n = 0;
   SDL_Window** windows = SDL_GetWindows(&n);
   if (windows != nullptr && n > 0)
      windowID = SDL_GetWindowID(windows[0]);
   if (windows) SDL_free(windows);
   return windowID;
}

// Arbitrary non-touch, non-pen SDL_MouseID. SDLInputHandler filters out
// SDL_TOUCH_MOUSEID and SDL_PEN_MOUSEID; any other value is accepted as
// a real mouse. The game doesn't care which mouse, only that it's not
// a touch or pen surrogate.
static constexpr SDL_MouseID kForwarderMouseID = 1;


void vpx_push_key_event(int isDown, unsigned short scancode)
{
   SDL_Event ev = {};
   ev.type = isDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
   ev.key.timestamp = SDL_GetTicksNS();
   ev.key.windowID = GetForwarderWindowID();
   ev.key.scancode = (SDL_Scancode)scancode;
   ev.key.key = SDL_GetKeyFromScancode((SDL_Scancode)scancode, SDL_KMOD_NONE, false);
   ev.key.down = isDown != 0;
   ev.key.repeat = false;
   SDL_PushEvent(&ev);
}


void vpx_push_mouse_button(int isDown, int button, float x, float y)
{
   SDL_Event ev = {};
   ev.type = isDown ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
   ev.button.timestamp = SDL_GetTicksNS();
   ev.button.windowID = GetForwarderWindowID();
   ev.button.which = kForwarderMouseID;
   ev.button.button = (Uint8)button;
   ev.button.down = isDown != 0;
   ev.button.clicks = 1;
   ev.button.x = x;
   ev.button.y = y;
   SDL_PushEvent(&ev);
}


void vpx_push_mouse_motion(float x, float y, float dx, float dy)
{
   SDL_Event ev = {};
   ev.type = SDL_EVENT_MOUSE_MOTION;
   ev.motion.timestamp = SDL_GetTicksNS();
   ev.motion.windowID = GetForwarderWindowID();
   ev.motion.which = kForwarderMouseID;
   ev.motion.state = 0;
   ev.motion.x = x;
   ev.motion.y = y;
   ev.motion.xrel = dx;
   ev.motion.yrel = dy;
   SDL_PushEvent(&ev);
}


void vpx_push_mouse_wheel(float x, float y)
{
   SDL_Event ev = {};
   ev.type = SDL_EVENT_MOUSE_WHEEL;
   ev.wheel.timestamp = SDL_GetTicksNS();
   ev.wheel.windowID = GetForwarderWindowID();
   ev.wheel.which = kForwarderMouseID;
   ev.wheel.x = x;
   ev.wheel.y = y;
   ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
   SDL_PushEvent(&ev);
}


static void* g_playfield_nswindow = nullptr;

void vpx_set_playfield_nswindow(void* nsWindow) { g_playfield_nswindow = nsWindow; }
void* vpx_get_playfield_nswindow(void)          { return g_playfield_nswindow; }




void vpx_pump_runloop_once()
{
   // Two-stage pump. We have to feed both:
   //   1. CFRunLoop sources — CADisplayLink, timers, dispatch_async to
   //      main queue. These power the game tick.
   //   2. NSEvents — keyboard, mouse, menu interactions. These power
   //      the SwiftUI/AppKit responsiveness.
   //
   // [NSRunLoop runMode:beforeDate:] handles (1) and also dispatches
   // NSEvents queued for the same mode, but only in that one mode.
   // Menu-tracking events live in a different mode, so we additionally
   // drain *all* modes via [NSApp nextEventMatchingMask:] -> sendEvent.
   //
   // Doing only the runloop pump loses menu/Cmd-Q responsiveness.
   // Doing only the event pump means CADisplayLink never fires and the
   // game loop freezes. We need both.
   @autoreleasepool {
      [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                               beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];

      NSEvent* event;
      while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                         untilDate:nil
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES])) {
         [NSApp sendEvent:event];
      }
   }
}




int vpx_run(int argc, char** argv)
{
   // Preserve the Ctrl-C handler that main.mm used to install.
   struct sigaction sa = {};
   sa.sa_handler = on_signal;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = 0;
   sigaction(SIGINT, &sa, nullptr);

   g_argc = argc;
   g_argv = argv;

   int rc = vpx_init();
   if (rc == 0)
      rc = vpx_run_command();
   vpx_shutdown();

   // vpx_run_command returned because Player::GameLoop's runloop pump
   // exited (close state changed). Tell SwiftUI to wind down so the
   // process actually exits — without this NSApp keeps running with no
   // game behind it and no window worth interacting with.
   [NSApp terminate:nil];
   return rc;
}
