// license:GPLv3+

#pragma once

#include "InputManager.h"

// SDLInputHandler — routes SDL keyboard, mouse, and touch events into
// VPX's InputManager. Joystick/gamepad input is handled separately by
// GCInputHandler (GameController.framework) on macOS; pinball-specific
// HID hardware (Pinscape, Arcade2TV-XR) would need IOHIDManager and
// isn't covered yet.

class SDLInputHandler final : public InputManager::InputHandler
{
public:
   explicit SDLInputHandler(InputManager& pininput)
      : m_pininput(pininput)
   {
      PLOGI << "SDL input handler registered (keyboard / mouse / touch)";
   }

   SDLInputHandler& operator=(SDLInputHandler&&) = delete;

   void Update() override {}

   void HandleSDLEvent(const SDL_Event& e)
   {
      switch (e.type)
      {
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
         if (e.key.repeat == 0)
            m_pininput.PushButtonEvent(m_pininput.GetKeyboardDeviceId(), static_cast<uint16_t>(e.key.scancode), e.key.timestamp, e.key.down);
         break;

      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
         if (e.button.which != SDL_TOUCH_MOUSEID && e.button.which != SDL_PEN_MOUSEID)
            m_pininput.PushButtonEvent(m_pininput.GetMouseDeviceId(), e.button.button, e.button.timestamp, e.button.down);
         break;

      case SDL_EVENT_FINGER_DOWN:
      case SDL_EVENT_FINGER_UP:
         if (e.tfinger.windowID == SDL_GetWindowID(g_pplayer->m_playfieldWnd->GetCore()))
            m_pininput.PushTouchEvent(e.tfinger.x, e.tfinger.y, e.tfinger.timestamp, e.type == SDL_EVENT_FINGER_DOWN);
         break;

      default: break;
      }
   }

private:
   InputManager& m_pininput;
};
