// license:GPLv3+

#pragma once

// C-ABI bridge between GCInputHandler.cpp (C++ side, sees InputManager
// and Wine's BOOL) and GCInputHandler_objc.mm (Obj-C++ side, sees
// GameController.framework and Apple's BOOL). The two sides can't
// share a translation unit because Wine's windows.h and Apple's
// objc.h both typedef BOOL to mutually incompatible types with no
// "already-defined" guard.
//
// Functions here are extern "C" and use only primitive types, so this
// header is safe to include from either side.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to a GCInputHandler instance, passed back from the
// Obj-C side when forwarding events.
typedef struct GCInputHandlerOpaque* GCInputHandlerRef;

// === Called from .cpp into .mm ===

// Set up GameController observers and scan currently-connected pads.
// `handler` is passed back to the event callbacks below to identify
// which GCInputHandler the event belongs to.
void gc_objc_install(GCInputHandlerRef handler);

// Tear down observers. Idempotent.
void gc_objc_uninstall(GCInputHandlerRef handler);

// === Called from .mm into .cpp ===

// Register a newly-connected controller. Returns the deviceId the
// .cpp side picked, which the .mm then quotes back on per-event calls.
uint16_t gc_cpp_register_device(GCInputHandlerRef handler, const char* settingId, const char* friendlyName);

void gc_cpp_unregister_device(GCInputHandlerRef handler, uint16_t deviceId);

// Friendly name shown in the in-game binding UI.
// `isAxis` is 0 for buttons, 1 for axes.
void gc_cpp_register_element_name(GCInputHandlerRef handler, uint16_t deviceId, int isAxis, uint16_t elementId, const char* name);

// Event push. `down` is 0/1 for buttons; `value` is in -1..1 for axes.
void gc_cpp_push_button(GCInputHandlerRef handler, uint16_t deviceId, uint16_t buttonId, int down);
void gc_cpp_push_axis(GCInputHandlerRef handler, uint16_t deviceId, uint16_t axisId, float value);

// Install the default Xbox-layout mapping for a newly-registered device.
void gc_cpp_install_default_mapping(GCInputHandlerRef handler, uint16_t deviceId);

#ifdef __cplusplus
}
#endif
