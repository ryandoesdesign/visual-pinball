// license:GPLv3+

#include "core/stdafx.h"
#include "GCInputHandler.h"
#include "GCInputHandlerBridge.h"


// Constants shared with the .mm side via the default-mapping callbacks.
// (The .mm uses parallel constants for its own event dispatch; keeping
// the values in sync is the price of the C-ABI bridge.)
namespace
{
   constexpr uint16_t kButtonSouth     = 0;
   constexpr uint16_t kButtonEast      = 1;
   constexpr uint16_t kButtonNorth     = 3;
   constexpr uint16_t kButtonLB        = 4;
   constexpr uint16_t kButtonRB        = 5;
   constexpr uint16_t kButtonView      = 6;
   constexpr uint16_t kButtonDpadUp    = 11;
   constexpr uint16_t kButtonDpadDown  = 12;
   constexpr uint16_t kButtonDpadLeft  = 13;
   constexpr uint16_t kButtonDpadRight = 14;

   constexpr uint16_t kAxisLeftX  = 0x0200;
   constexpr uint16_t kAxisLeftY  = 0x0201;
   constexpr uint16_t kAxisRightY = 0x0203;
   constexpr uint16_t kAxisLT     = 0x0204;
   constexpr uint16_t kAxisRT     = 0x0205;
}


struct GCInputHandler::Impl
{
   InputManager& pininput;
};


GCInputHandler::GCInputHandler(InputManager& pininput)
   : m_impl(std::make_unique<Impl>(pininput))
{
   PLOGI << "GameController input handler registered";
   gc_objc_install(reinterpret_cast<GCInputHandlerRef>(this));
}


GCInputHandler::~GCInputHandler()
{
   gc_objc_uninstall(reinterpret_cast<GCInputHandlerRef>(this));
}


void GCInputHandler::PlayRumble(float, float, int)
{
   // TODO: route to GCController.haptics. Doing it right needs
   // CHHapticEngine setup per controller — stub for this slice.
}


// === C-ABI bridge functions called from the Obj-C++ side ===

extern "C" uint16_t gc_cpp_register_device(GCInputHandlerRef handler, const char* settingId, const char* friendlyName)
{
   auto* self = reinterpret_cast<GCInputHandler*>(handler);
   return self->m_impl->pininput.RegisterDevice(settingId, InputManager::DeviceType::Joystick, friendlyName);
}

extern "C" void gc_cpp_unregister_device(GCInputHandlerRef handler, uint16_t deviceId)
{
   auto* self = reinterpret_cast<GCInputHandler*>(handler);
   self->m_impl->pininput.UnregisterDevice(deviceId);
}

extern "C" void gc_cpp_register_element_name(GCInputHandlerRef handler, uint16_t deviceId, int isAxis, uint16_t elementId, const char* name)
{
   auto* self = reinterpret_cast<GCInputHandler*>(handler);
   self->m_impl->pininput.RegisterElementName(deviceId, isAxis != 0, elementId, name);
}

extern "C" void gc_cpp_push_button(GCInputHandlerRef handler, uint16_t deviceId, uint16_t buttonId, int down)
{
   auto* self = reinterpret_cast<GCInputHandler*>(handler);
   self->m_impl->pininput.PushButtonEvent(deviceId, buttonId, SDL_GetTicksNS(), down != 0);
}

extern "C" void gc_cpp_push_axis(GCInputHandlerRef handler, uint16_t deviceId, uint16_t axisId, float value)
{
   auto* self = reinterpret_cast<GCInputHandler*>(handler);
   self->m_impl->pininput.PushAxisEvent(deviceId, axisId, SDL_GetTicksNS(), value);
}

extern "C" void gc_cpp_install_default_mapping(GCInputHandlerRef handler, uint16_t deviceId)
{
   // Mirrors the "generic gamepad" default from the old SDLInputHandler:
   // triggers for flippers, shoulders for magna-save, face buttons for
   // launch/start/coin, dpad for service menu + UI nav, right stick
   // for plunger, left stick for nudge.
   auto* self = reinterpret_cast<GCInputHandler*>(handler);
   InputManager& mgr = self->m_impl->pininput;
   mgr.RegisterDefaultMapping(deviceId,
      [&mgr, deviceId](
         const std::function<bool(const vector<ButtonMapping>&, unsigned int)>& mapButton,
         const std::function<bool(const SensorMapping&, SensorMapping::Type, bool)>& mapPlunger,
         const std::function<bool(const SensorMapping&, const SensorMapping&)>& mapNudge)
      {
         bool ok = true;
         ok &= mapButton(ButtonMapping::Create(deviceId, kAxisLT, -0.3f), mgr.GetLeftFlipperActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kAxisRT, -0.3f), mgr.GetRightFlipperActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kAxisLT,  0.3f), mgr.GetStagedLeftFlipperActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kAxisRT,  0.3f), mgr.GetStagedRightFlipperActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonLB),    mgr.GetLeftMagnaActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonRB),    mgr.GetRightMagnaActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonNorth), mgr.GetAddCreditActionId(0));
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonEast),  mgr.GetStartActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonSouth), mgr.GetLaunchBallActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonView),  mgr.GetOpenInGameUIActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonDpadLeft),  mgr.GetServiceActionId(0));
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonDpadDown),  mgr.GetServiceActionId(1));
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonDpadUp),    mgr.GetServiceActionId(2));
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonDpadRight), mgr.GetServiceActionId(3));
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonDpadUp),    mgr.GetUIUpActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonDpadDown),  mgr.GetUIDownActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonDpadLeft),  mgr.GetUILeftActionId());
         ok &= mapButton(ButtonMapping::Create(deviceId, kButtonDpadRight), mgr.GetUIRightActionId());
         ok &= mapPlunger(SensorMapping::Create(deviceId, kAxisRightY, SensorMapping::Type::Position), SensorMapping::Type::Position, true);
         ok &= mapNudge(
            SensorMapping::Create(deviceId, kAxisLeftX, SensorMapping::Type::Position),
            SensorMapping::Create(deviceId, kAxisLeftY, SensorMapping::Type::Position));
         return ok;
      });
}
