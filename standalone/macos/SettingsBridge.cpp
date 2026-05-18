// SwiftUI Settings window — engine bridge.
// Pure C++ so we avoid the Wine-BOOL vs Apple-BOOL collision that
// .mm files hit when including core/stdafx.h. Foundation isn't needed
// — every Apple API the SwiftUI side wants lives on the Swift side.

#include "core/stdafx.h"

#include "core/player.h"
#include "core/extern.h"   // g_pplayer
#include "renderer/Window.h"
#include "renderer/RenderDevice.h"
#include "renderer/Renderer.h"

#include "standalone/macos/SettingsBridge.h"

#include <algorithm>
#include <array>
#include <vector>
#include <cstring>

#include <dispatch/dispatch.h>


// Translate the public ABI window id into the engine's enum.
static VPXWindowId map_window_id(vpx_window_id_t id)
{
   switch (id)
   {
   case VPX_WND_PLAYFIELD:  return VPXWindowId::VPXWINDOW_Playfield;
   case VPX_WND_VR_PREVIEW: return VPXWindowId::VPXWINDOW_VRPreview;
   case VPX_WND_BACKGLASS:  return VPXWindowId::VPXWINDOW_Backglass;
   case VPX_WND_SCOREVIEW:  return VPXWindowId::VPXWINDOW_ScoreView;
   case VPX_WND_TOPPER:     return VPXWindowId::VPXWINDOW_Topper;
   }
   return VPXWindowId::VPXWINDOW_Playfield;
}

// Return the engine Window* for an ABI window id, or nullptr if not
// realised. This slice only handles the playfield; ancillary windows
// can be added in a follow-up slice.
static VPX::Window* get_window(vpx_window_id_t id)
{
   if (!g_pplayer) return nullptr;
   if (id == VPX_WND_PLAYFIELD) return g_pplayer->m_playfieldWnd;
   return nullptr;
}

// In-memory only — UI state, not persisted (matches the comment at
// DisplaySettingsPage.cpp:378 "UI state, not persisted"). One slot per
// window id; main-thread-only access (SwiftUI).
static int s_arLock[5] = { 0, 0, 0, 0, 0 };


extern "C" int vpx_settings_is_player_active(void)
{
   return g_pplayer != nullptr ? 1 : 0;
}

extern "C" int vpx_settings_get_displays(vpx_display_info_t* buf, int max)
{
   if (!buf || max <= 0) return 0;
   const auto displays = VPX::Window::GetDisplays();
   const int n = std::min(static_cast<int>(displays.size()), max);
   for (int i = 0; i < n; ++i)
   {
      buf[i].index = i;
      buf[i].width = displays[i].width;
      buf[i].height = displays[i].height;
      buf[i].is_primary = displays[i].isPrimary ? 1 : 0;
      std::snprintf(buf[i].name, sizeof(buf[i].name), "%s",
         displays[i].displayName.c_str());
   }
   return n;
}

extern "C" int vpx_settings_get_window_position(vpx_window_id_t id, int* x, int* y)
{
   VPX::Window* w = get_window(id);
   if (!w || !x || !y) return 0;
   w->GetPos(*x, *y);
   return 1;
}

extern "C" int vpx_settings_get_window_size(vpx_window_id_t id, int* outW, int* outH)
{
   VPX::Window* w = get_window(id);
   if (!w || !outW || !outH) return 0;
   *outW = w->GetWidth();
   *outH = w->GetHeight();
   return 1;
}

extern "C" int vpx_settings_get_window_display(vpx_window_id_t id)
{
   VPX::Window* w = get_window(id);
   if (!w) return -1;
   // Find the display whose bounds contain the window's centre.
   int x = 0, y = 0;
   w->GetPos(x, y);
   const int cx = x + w->GetWidth()  / 2;
   const int cy = y + w->GetHeight() / 2;
   const auto displays = VPX::Window::GetDisplays();
   for (size_t i = 0; i < displays.size(); ++i)
   {
      const auto& d = displays[i];
      if (cx >= d.left && cx < d.left + d.width
       && cy >= d.top  && cy < d.top  + d.height)
         return static_cast<int>(i);
   }
   return 0; // fallback to first display
}

extern "C" int vpx_settings_get_window_arlock(vpx_window_id_t id)
{
   if (id < 0 || id >= static_cast<int>(std::size(s_arLock))) return 0;
   return s_arLock[id];
}

// --- Writers ---------------------------------------------------------
//
// Window mutations end up calling Cocoa's NSWindow APIs (via SDL's
// Cocoa video driver) and macOS 14+ asserts "Must only be used from
// the main thread" on those. Marshal each mutation to the main queue
// via libdispatch. The block captures a heap-allocated std::function
// so the lambda survives across the queue hop. The main thread is
// where the game loop also lives (Player::GameLoop pumps the runloop
// via vpx_pump_runloop_once each tick), so dispatched blocks fire
// during the pump without backing up.
//
// Don't use RenderDevice::AddEndOfFrameCmd here — that's safe for
// GPU/Settings state but NOT for NSWindow APIs.

static void queue_main_thread(std::function<void()> cmd)
{
   auto* heapCmd = new std::function<void()>(std::move(cmd));
   dispatch_async(dispatch_get_main_queue(), ^{
      (*heapCmd)();
      delete heapCmd;
   });
}

extern "C" void vpx_settings_set_window_position(vpx_window_id_t id, int x, int y)
{
   if (!g_pplayer || id != VPX_WND_PLAYFIELD) return;
   queue_main_thread([x, y]() {
      if (!g_pplayer || !g_pplayer->m_playfieldWnd) return;
      g_pplayer->m_playfieldWnd->SetPos(x, y);
   });
}

extern "C" void vpx_settings_set_window_size(vpx_window_id_t id, int w, int h)
{
   if (!g_pplayer || id != VPX_WND_PLAYFIELD) return;
   queue_main_thread([w, h]() {
      if (!g_pplayer || !g_pplayer->m_playfieldWnd) return;
      g_pplayer->m_playfieldWnd->SetSize(w, h);
   });
}

extern "C" void vpx_settings_set_window_display(vpx_window_id_t id, int display_index)
{
   if (!g_pplayer || id != VPX_WND_PLAYFIELD) return;
   queue_main_thread([display_index]() {
      if (!g_pplayer || !g_pplayer->m_playfieldWnd) return;
      const auto displays = VPX::Window::GetDisplays();
      if (display_index < 0 || display_index >= static_cast<int>(displays.size())) return;
      VPX::Window* const win = g_pplayer->m_playfieldWnd;
      const int sx = displays[display_index].left + (displays[display_index].width  - win->GetWidth())  / 2;
      const int sy = displays[display_index].top  + (displays[display_index].height - win->GetHeight()) / 2;
      win->SetPos(sx, sy);
   });
}

extern "C" void vpx_settings_set_window_arlock(vpx_window_id_t id, int arlock)
{
   if (id < 0 || id >= static_cast<int>(std::size(s_arLock))) return;
   s_arLock[id] = arlock;
}
