// license:GPLv3+

#pragma once

#include "InputManager.h"
#include <memory>

// GCInputHandler — gamepad input via Apple's GameController.framework.
//
// Replaces SDLInputHandler's joystick/gamepad path on macOS. SDL still
// owns keyboard/mouse/touch event routing; this class is purely for
// gamepads (MFi / Extended Profile controllers: Xbox, PlayStation,
// Nintendo, MFi).
//
// Pinball-specific HID hardware (Pinscape, Arcade2TV-XR) doesn't show
// up to GameController.framework — those would need IOHIDManager
// directly. Out of scope for this slice.
//
// The class lives in C++-only headers; the Obj-C++ implementation
// (GameController API + NotificationCenter observers) is hidden
// behind PIMPL so consumers can include this file from pure C++.

class GCInputHandler final : public InputManager::InputHandler
{
public:
   explicit GCInputHandler(InputManager& pininput);
   ~GCInputHandler() override;

   GCInputHandler(const GCInputHandler&) = delete;
   GCInputHandler& operator=(const GCInputHandler&) = delete;

   // Game-loop tick. No-op: GameController.framework drives input via
   // value-changed handlers, not polling.
   void Update() override {}

   // Haptic rumble. Routes to GCController.haptics if the controller
   // supports it; otherwise a no-op.
   void PlayRumble(float lowFrequencySpeed, float highFrequencySpeed, int ms_duration) override;

   // Exposed (not really private) so the C-ABI bridge functions in
   // GCInputHandler.cpp can reach InputManager& from the callbacks
   // dispatched by the Obj-C++ side. Not a real encapsulation
   // boundary either way.
   struct Impl;
   std::unique_ptr<Impl> m_impl;
};
