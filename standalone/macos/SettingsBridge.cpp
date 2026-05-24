// SwiftUI Settings window — engine bridge.
// Pure C++ so we avoid the Wine-BOOL vs Apple-BOOL collision that
// .mm files hit when including core/stdafx.h. Foundation isn't needed
// — every Apple API the SwiftUI side wants lives on the Swift side.

#include "core/stdafx.h"

#include "core/player.h"
#include "core/extern.h"   // g_pplayer
#include "core/VPApp.h"
#include "core/FileLocator.h"
#include "parts/pintable.h"
#include "renderer/Window.h"
#include "renderer/RenderDevice.h"
#include "renderer/Renderer.h"
#include "renderer/ViewSetup.h"
#include "audio/AudioPlayer.h"

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


// ---- View / camera --------------------------------------------------

static float* view_field(ViewSetup& vs, vpx_view_property_t prop)
{
   switch (prop)
   {
   case VPX_VIEW_FOV:      return &vs.mFOV;
   case VPX_VIEW_LOOK_AT:  return &vs.mLookAt;
   case VPX_VIEW_LAYBACK:  return &vs.mLayback;
   case VPX_VIEW_SCALE_X:  return &vs.mSceneScaleX;
   case VPX_VIEW_SCALE_Y:  return &vs.mSceneScaleY;
   case VPX_VIEW_SCALE_Z:  return &vs.mSceneScaleZ;
   case VPX_VIEW_HOFS:     return &vs.mViewHOfs;
   case VPX_VIEW_VOFS:     return &vs.mViewVOfs;
   case VPX_VIEW_ROTATION: return &vs.mViewportRotation;
   }
   return nullptr;
}

extern "C" float vpx_view_get(vpx_view_property_t prop)
{
   if (!g_pplayer || !g_pplayer->m_ptable) return 0.f;
   const float* f = view_field(g_pplayer->m_ptable->GetViewSetup(), prop);
   return f ? *f : 0.f;
}

extern "C" void vpx_view_set(vpx_view_property_t prop, float value)
{
   if (!g_pplayer || !g_pplayer->m_ptable || !g_pplayer->m_renderer
    || !g_pplayer->m_renderer->m_renderDevice)
      return;
   // ViewSetup mutation + DisableStaticPrePass + InitLayout are all
   // render-thread work — no NSWindow APIs involved, so AddEndOfFrameCmd
   // is the right queue (unlike the window-position/size setters).
   g_pplayer->m_renderer->m_renderDevice->AddEndOfFrameCmd([prop, value]() {
      if (!g_pplayer || !g_pplayer->m_ptable || !g_pplayer->m_renderer)
         return;
      float* f = view_field(g_pplayer->m_ptable->GetViewSetup(), prop);
      if (!f) return;
      *f = value;
      g_pplayer->m_renderer->DisableStaticPrePass(true);
      g_pplayer->m_renderer->InitLayout();
   });
}


// --- Default preset (applied during table load) ----------------------

namespace {
struct DefaultPreset
{
   bool  enabled = false;
   float fov = 45;
   float lookAt = 25;
   float layback = 0;
   float scaleX = 1;
   float scaleY = 1;
   float scaleZ = 1;
   float hOfs = 0;
   float vOfs = 0;
   float rotation = 0;
};
DefaultPreset s_defaultPreset;
}

extern "C" void vpx_view_set_default_preset(int enabled,
                                            float fov, float lookAt, float layback,
                                            float scaleX, float scaleY, float scaleZ,
                                            float hOfs, float vOfs, float rotation)
{
   s_defaultPreset = DefaultPreset {
      enabled != 0, fov, lookAt, layback, scaleX, scaleY, scaleZ, hOfs, vOfs, rotation
   };
}

extern "C" void vpx_view_internal_apply_default_preset_on_load(void)
{
   if (!s_defaultPreset.enabled) return;
   if (!g_pplayer || !g_pplayer->m_ptable) return;
   // Apply to all three view modes so switching mode mid-game keeps
   // the preset consistent. The active mode's ViewSetup is what the
   // renderer reads via GetViewSetup() during InitLayout — that runs
   // later in Player::Player, so our writes here land before the
   // first frame.
   for (int i = 0; i < 3; ++i)
   {
      ViewSetup& vs = g_pplayer->m_ptable->mViewSetups[i];
      vs.mFOV              = s_defaultPreset.fov;
      vs.mLookAt           = s_defaultPreset.lookAt;
      vs.mLayback          = s_defaultPreset.layback;
      vs.mSceneScaleX      = s_defaultPreset.scaleX;
      vs.mSceneScaleY      = s_defaultPreset.scaleY;
      vs.mSceneScaleZ      = s_defaultPreset.scaleZ;
      vs.mViewHOfs         = s_defaultPreset.hOfs;
      vs.mViewVOfs         = s_defaultPreset.vOfs;
      vs.mViewportRotation = s_defaultPreset.rotation;
   }
}


// ---- Audio ----------------------------------------------------------

// UI-only state — the "lock volumes" toggle isn't backed by an engine
// field. Matches AudioSettingsPage::m_lockVolume (default true).
static bool s_lockVolumes = true;

// Queue a mutation that touches Player / AudioPlayer state. Uses the
// render-thread end-of-frame queue (same place the ImGui page's
// callbacks already run), so AudioPlayer reconstruction and miniaudio
// volume changes happen in a thread-safe context. Don't use
// queue_main_thread here — that's for NSWindow APIs.
static void queue_render_thread(std::function<void()> cmd)
{
   if (!g_pplayer || !g_pplayer->m_renderer
    || !g_pplayer->m_renderer->m_renderDevice)
      return;
   g_pplayer->m_renderer->m_renderDevice->AddEndOfFrameCmd(std::move(cmd));
}

extern "C" int vpx_audio_get_music_volume(void)
{
   return g_pplayer ? g_pplayer->m_MusicVolume : 0;
}

extern "C" int vpx_audio_get_sound_volume(void)
{
   return g_pplayer ? g_pplayer->m_SoundVolume : 0;
}

extern "C" int vpx_audio_get_play_music(void)
{
   return (g_pplayer && g_pplayer->m_PlayMusic) ? 1 : 0;
}

extern "C" int vpx_audio_get_play_sound(void)
{
   return (g_pplayer && g_pplayer->m_PlaySound) ? 1 : 0;
}

extern "C" int vpx_audio_get_lock_volumes(void)
{
   return s_lockVolumes ? 1 : 0;
}

extern "C" int vpx_audio_get_sound3d_mode(void)
{
   if (!g_pplayer || !g_pplayer->m_audioPlayer) return 0;
   return static_cast<int>(g_pplayer->m_audioPlayer->GetSoundMode3D());
}

extern "C" void vpx_audio_set_music_volume(int v)
{
   if (!g_pplayer) return;
   const int clamped = std::clamp(v, 0, 100);
   queue_render_thread([clamped]() {
      if (!g_pplayer) return;
      g_pplayer->m_MusicVolume = clamped;
      g_pplayer->UpdateVolume();
   });
}

extern "C" void vpx_audio_set_sound_volume(int v)
{
   if (!g_pplayer) return;
   const int clamped = std::clamp(v, 0, 100);
   queue_render_thread([clamped]() {
      if (!g_pplayer) return;
      g_pplayer->m_SoundVolume = clamped;
      g_pplayer->UpdateVolume();
   });
}

extern "C" void vpx_audio_set_play_music(int v)
{
   if (!g_pplayer) return;
   const bool on = v != 0;
   queue_render_thread([on]() {
      if (!g_pplayer) return;
      g_pplayer->m_PlayMusic = on;
      g_pplayer->UpdateVolume();
   });
}

extern "C" void vpx_audio_set_play_sound(int v)
{
   if (!g_pplayer) return;
   const bool on = v != 0;
   queue_render_thread([on]() {
      if (!g_pplayer) return;
      g_pplayer->m_PlaySound = on;
      g_pplayer->UpdateVolume();
   });
}

extern "C" void vpx_audio_set_lock_volumes(int v)
{
   s_lockVolumes = v != 0;
}

extern "C" void vpx_audio_set_sound3d_mode(int mode)
{
   if (!g_pplayer || !g_pplayer->m_ptable) return;
   if (mode < 0 || mode > static_cast<int>(VPX::SNDCFG_SND3DSSF)) return;
   queue_render_thread([mode]() {
      if (!g_pplayer || !g_pplayer->m_ptable) return;
      const auto& s = g_pplayer->m_ptable->m_settings;
      g_pplayer->m_audioPlayer = std::make_unique<VPX::AudioPlayer>(
         s.GetPlayer_SoundDeviceBG(),
         s.GetPlayer_SoundDevice(),
         static_cast<VPX::SoundConfigTypes>(mode));
   });
}

extern "C" int vpx_audio_get_devices(vpx_audio_device_t* buf, int max)
{
   if (!buf || max <= 0) return 0;
   const auto devices = VPX::AudioPlayer::EnumerateAudioDevices();
   const int n = std::min(static_cast<int>(devices.size()), max);
   for (int i = 0; i < n; ++i)
   {
      buf[i].index = i;
      buf[i].channels = static_cast<int>(devices[i].channels);
      std::snprintf(buf[i].name, sizeof(buf[i].name), "%s", devices[i].name.c_str());
   }
   return n;
}

// Resolve a live device-name string to its index in EnumerateAudioDevices().
// Returns -1 if no match (e.g. a device that was unplugged after the
// AudioPlayer was constructed).
static int find_device_index(const string& name)
{
   const auto devices = VPX::AudioPlayer::EnumerateAudioDevices();
   for (size_t i = 0; i < devices.size(); ++i)
      if (devices[i].name == name) return static_cast<int>(i);
   return -1;
}

extern "C" int vpx_audio_get_backglass_device(void)
{
   if (!g_pplayer || !g_pplayer->m_audioPlayer) return -1;
   return find_device_index(g_pplayer->m_audioPlayer->GetBackglassDeviceName());
}

extern "C" int vpx_audio_get_playfield_device(void)
{
   if (!g_pplayer || !g_pplayer->m_audioPlayer) return -1;
   return find_device_index(g_pplayer->m_audioPlayer->GetPlayfieldDeviceName());
}

extern "C" void vpx_audio_set_backglass_device(int device_index)
{
   if (!g_pplayer || !g_pplayer->m_ptable) return;
   queue_render_thread([device_index]() {
      if (!g_pplayer || !g_pplayer->m_ptable) return;
      const auto devices = VPX::AudioPlayer::EnumerateAudioDevices();
      if (device_index < 0 || device_index >= static_cast<int>(devices.size())) return;
      const auto& s = g_pplayer->m_ptable->m_settings;
      g_pplayer->m_audioPlayer = std::make_unique<VPX::AudioPlayer>(
         devices[device_index].name,
         s.GetPlayer_SoundDevice(),
         static_cast<VPX::SoundConfigTypes>(s.GetPlayer_Sound3D()));
   });
}

extern "C" void vpx_audio_set_playfield_device(int device_index)
{
   if (!g_pplayer || !g_pplayer->m_ptable) return;
   queue_render_thread([device_index]() {
      if (!g_pplayer || !g_pplayer->m_ptable) return;
      const auto devices = VPX::AudioPlayer::EnumerateAudioDevices();
      if (device_index < 0 || device_index >= static_cast<int>(devices.size())) return;
      const auto& s = g_pplayer->m_ptable->m_settings;
      g_pplayer->m_audioPlayer = std::make_unique<VPX::AudioPlayer>(
         s.GetPlayer_SoundDeviceBG(),
         devices[device_index].name,
         static_cast<VPX::SoundConfigTypes>(s.GetPlayer_Sound3D()));
   });
}

extern "C" int vpx_get_log_path(char* buf, int buf_size)
{
   if (!buf || buf_size <= 0) return 0;
   buf[0] = '\0';
   if (!g_app) return 0;
   const std::string path = g_app->m_fileLocator
      .GetAppPath(FileLocator::AppSubFolder::Preferences, "vpinball.log")
      .string();
   const int n = std::snprintf(buf, buf_size, "%s", path.c_str());
   return (n < 0) ? 0 : std::min(n, buf_size - 1);
}


// ---- Loading progress -----------------------------------------------
//
// Three small main-thread-only fields. Both writers (ProgressDialog
// from the engine, the SwiftUI side for app-launch phases) are on the
// main thread, so no synchronisation is needed.

static char s_loadingText[256] = {0};
static int  s_loadingPercent = -1;
static int  s_loadingActive = 0;
static int  s_emulatorWarming = 0;   // sticky — see vpx_loading_emulator_starting

extern "C" void vpx_loading_set(const char* text, int percent)
{
   // The PinMAME warmup overlay outranks engine progress — once the
   // emulator is booting we don't want the engine's "Starting…" /
   // "Prerendering 100 %" lines clobbering "Warming up emulator…".
   if (s_emulatorWarming) return;

   if (text)
      std::snprintf(s_loadingText, sizeof(s_loadingText), "%s", text);
   if (percent >= 0)
      s_loadingPercent = percent;
}

extern "C" void vpx_loading_set_active(int active)
{
   s_loadingActive = active ? 1 : 0;
   if (!active)
   {
      s_loadingText[0] = '\0';
      s_loadingPercent = -1;
   }
}

extern "C" int vpx_loading_get(char* text_buf, int text_buf_size, int* percent)
{
   if (text_buf && text_buf_size > 0)
      std::snprintf(text_buf, text_buf_size, "%s", s_loadingText);
   if (percent)
      *percent = s_loadingPercent;
   return s_loadingActive;
}

extern "C" void vpx_loading_emulator_starting(void)
{
   s_emulatorWarming = 1;
   std::snprintf(s_loadingText, sizeof(s_loadingText), "%s",
      "Warming up emulator…");
   s_loadingPercent = -1;
   s_loadingActive = 1;
}

// Audio-arrived tally — see header. Host's Player::OnAudioUpdated
// calls vpx_loading_audio_arrived() each time a plugin pushes a
// buffer; the Swift overlay uses the counter as "emulator is alive".
static int s_audioCount = 0;

extern "C" void vpx_loading_audio_arrived(void)
{
   ++s_audioCount;
   // If we were showing the warmup overlay, drop the sticky flag so
   // Swift's next tick can see fresh engine state and hide cleanly.
   if (s_emulatorWarming)
   {
      s_emulatorWarming = 0;
      s_loadingActive = 0;
      s_loadingText[0] = '\0';
      s_loadingPercent = -1;
   }
}

extern "C" void vpx_loading_audio_reset(void)
{
   s_audioCount = 0;
   s_emulatorWarming = 0;
}

extern "C" int vpx_loading_get_audio_count(void)
{
   return s_audioCount;
}
