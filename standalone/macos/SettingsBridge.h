// C ABI for the SwiftUI Settings window. Mirrors the
// ImageIOBridge.h pattern: pure C, opaque types, no Foundation.
// Implementation lives in SettingsBridge.mm.
//
// Threading: all functions are safe to call from the SwiftUI main
// thread. Reads return current engine state synchronously. Writes are
// queued via RenderDevice::AddEndOfFrameCmd so the actual mutation
// runs on the render thread at end-of-frame, matching the existing
// in-game UI's mutation pattern.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors VPXWindowId in src/renderer/Window.h. Stable wire ABI.
typedef enum {
   VPX_WND_PLAYFIELD   = 0,
   VPX_WND_VR_PREVIEW  = 1,
   VPX_WND_BACKGLASS   = 2,
   VPX_WND_SCOREVIEW   = 3,
   VPX_WND_TOPPER      = 4,
} vpx_window_id_t;

typedef struct {
   int  index;            // index in the platform's display enumeration
   int  width;            // pixels
   int  height;           // pixels
   int  is_primary;       // 1 if this is the system's primary display
   char name[128];        // human-readable display name (NUL-terminated)
} vpx_display_info_t;

// Is a Player active (table loaded, render loop running)? When 0, the
// SwiftUI Settings UI should grey out controls — every getter returns 0
// and every setter is a no-op.
int  vpx_settings_is_player_active(void);

// Fill `buf` with up to `max` entries describing available displays.
// Returns the count actually written (0 if no player active).
int  vpx_settings_get_displays(vpx_display_info_t* buf, int max);

// Read live state of a window. Returns 1 on success, 0 if player
// inactive or window not realised.
int  vpx_settings_get_window_position(vpx_window_id_t, int* x, int* y);
int  vpx_settings_get_window_size    (vpx_window_id_t, int* w, int* h);
int  vpx_settings_get_window_display (vpx_window_id_t);   // index into vpx_settings_get_displays; <0 on failure
int  vpx_settings_get_window_arlock  (vpx_window_id_t);   // 0=free, 1..N indexed into the aspect-ratio table (SwiftUI-side)

// Queue a mutation. No-op if no player active. Width/height ignore the
// AR lock; pass already-constrained values from the SwiftUI side
// (matches the existing ImGui page's lambda-side enforcement).
void vpx_settings_set_window_position(vpx_window_id_t, int x, int y);
void vpx_settings_set_window_size    (vpx_window_id_t, int w, int h);
void vpx_settings_set_window_display (vpx_window_id_t, int display_index);
void vpx_settings_set_window_arlock  (vpx_window_id_t, int arlock);


// ---- View / camera (Point Of View) -----------------------------------
//
// Properties of the currently-active table's ViewSetup. Mutations apply
// to whichever view mode is in use (Desktop / FSS / Cabinet) — the
// engine's ViewSetup struct is shared between modes from the SwiftUI
// side's perspective. Setters queue a render-thread command that
// mutates the struct and calls Renderer::DisableStaticPrePass +
// InitLayout, mirroring the ImGui page's OnPointOfViewChanged.

typedef enum {
   VPX_VIEW_FOV         = 0,   // degrees
   VPX_VIEW_LOOK_AT     = 1,   // %
   VPX_VIEW_LAYBACK     = 2,   // legacy skew angle
   VPX_VIEW_SCALE_X     = 3,
   VPX_VIEW_SCALE_Y     = 4,
   VPX_VIEW_SCALE_Z     = 5,
   VPX_VIEW_HOFS        = 6,   // horizontal frustum offset
   VPX_VIEW_VOFS        = 7,   // vertical frustum offset
   VPX_VIEW_ROTATION    = 8,   // viewport rotation, degrees
} vpx_view_property_t;

// Returns current value, or 0.0 if no player active.
float vpx_view_get(vpx_view_property_t);

// Queue a mutation; no-op if no player active.
void  vpx_view_set(vpx_view_property_t, float value);

// Register a "default preset" that the engine will apply to every
// view mode of every table it loads, replacing whatever the table
// stored. Lives in process memory only (not persisted). Call once at
// app launch with the preset values; the engine then naturally
// applies them as part of Player construction — no race, no retry.
// Set enabled=0 to disable (engine falls back to table's saved view).
void vpx_view_set_default_preset(int enabled,
                                 float fov, float look_at, float layback,
                                 float scale_x, float scale_y, float scale_z,
                                 float h_ofs, float v_ofs, float rotation);

// Called from Player::Player after ApplyTableOverrideSettings has run
// on each view mode. If a default preset is registered, overwrites
// the corresponding ViewSetup fields. Otherwise a no-op. Engine code
// path — not meant for SwiftUI use.
void vpx_view_internal_apply_default_preset_on_load(void);

#ifdef __cplusplus
} // extern "C"
#endif
