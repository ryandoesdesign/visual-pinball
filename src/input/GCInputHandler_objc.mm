// license:GPLv3+

// Obj-C++ half of GCInputHandler. Owns the GameController.framework
// integration. Communicates with the C++ half via the C-ABI in
// GCInputHandlerBridge.h — no Wine/Win32 headers reach this TU so
// Apple's BOOL stays intact.

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

#include "GCInputHandlerBridge.h"


// Mirror of the constants in GCInputHandler.cpp's default mapping.
// Keep these in sync if you reshuffle the button numbering.
namespace
{
   constexpr uint16_t kButtonSouth     = 0;
   constexpr uint16_t kButtonEast      = 1;
   constexpr uint16_t kButtonWest      = 2;
   constexpr uint16_t kButtonNorth     = 3;
   constexpr uint16_t kButtonLB        = 4;
   constexpr uint16_t kButtonRB        = 5;
   constexpr uint16_t kButtonView      = 6;
   constexpr uint16_t kButtonMenu      = 7;
   constexpr uint16_t kButtonHome      = 8;
   constexpr uint16_t kButtonL3        = 9;
   constexpr uint16_t kButtonR3        = 10;
   constexpr uint16_t kButtonDpadUp    = 11;
   constexpr uint16_t kButtonDpadDown  = 12;
   constexpr uint16_t kButtonDpadLeft  = 13;
   constexpr uint16_t kButtonDpadRight = 14;

   constexpr uint16_t kAxisLeftX  = 0x0200;
   constexpr uint16_t kAxisLeftY  = 0x0201;
   constexpr uint16_t kAxisRightX = 0x0202;
   constexpr uint16_t kAxisRightY = 0x0203;
   constexpr uint16_t kAxisLT     = 0x0204;
   constexpr uint16_t kAxisRT     = 0x0205;
}


// State per installed GCInputHandler. Holds the observer tokens and
// the controller→deviceId map. Lifetimes are managed by Obj-C ARC.
@interface VPXGamepadState : NSObject
@property (nonatomic) GCInputHandlerRef handler;
@property (nonatomic, strong) id connectObserver;
@property (nonatomic, strong) id disconnectObserver;
@property (nonatomic, strong) NSMapTable<GCController*, NSNumber*>* deviceIds;
@property (nonatomic, strong) NSMutableDictionary<NSString*, NSNumber*>* modelCounters;
@end
@implementation VPXGamepadState
@end


// One state per active handler. Keyed by the opaque handle so we can
// look it up from notification blocks without retain cycles.
static NSMutableDictionary<NSValue*, VPXGamepadState*>* g_states = nil;


static NSString* makeSettingId(VPXGamepadState* state, GCController* controller)
{
   NSString* category = controller.productCategory ?: @"GameController";
   NSMutableString* sanitized = [NSMutableString stringWithCapacity:category.length];
   for (NSUInteger i = 0; i < category.length; ++i)
   {
      unichar ch = [category characterAtIndex:i];
      const BOOL alnum = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
      [sanitized appendFormat:@"%C", alnum ? ch : (unichar)'_'];
   }
   NSString* base = [NSString stringWithFormat:@"GCPad_%@", sanitized];
   NSNumber* idx = state.modelCounters[base] ?: @0;
   state.modelCounters[base] = @(idx.intValue + 1);
   return [NSString stringWithFormat:@"%@_%d", base, idx.intValue + 1];
}


static void installValueHandlers(VPXGamepadState* state, GCController* controller, uint16_t deviceId)
{
   GCExtendedGamepad* gp = controller.extendedGamepad;
   GCInputHandlerRef h = state.handler;

   // Lambda-style block factories. NSObject blocks capture `h` and
   // `deviceId` by value (both POD), so no weak-ref dance needed.
   void (^btn)(uint16_t) = nil;  // dummy declaration to satisfy clang's analysis
   (void)btn;

   gp.buttonA.valueChangedHandler        = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonSouth, pressed); };
   gp.buttonB.valueChangedHandler        = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonEast,  pressed); };
   gp.buttonX.valueChangedHandler        = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonWest,  pressed); };
   gp.buttonY.valueChangedHandler        = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonNorth, pressed); };
   gp.leftShoulder.valueChangedHandler   = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonLB,    pressed); };
   gp.rightShoulder.valueChangedHandler  = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonRB,    pressed); };
   gp.buttonMenu.valueChangedHandler     = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonMenu,  pressed); };
   if (gp.buttonOptions)
      gp.buttonOptions.valueChangedHandler = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonView, pressed); };
   if (gp.buttonHome)
      gp.buttonHome.valueChangedHandler    = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonHome, pressed); };
   if (gp.leftThumbstickButton)
      gp.leftThumbstickButton.valueChangedHandler  = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonL3, pressed); };
   if (gp.rightThumbstickButton)
      gp.rightThumbstickButton.valueChangedHandler = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonR3, pressed); };

   gp.dpad.up.valueChangedHandler    = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonDpadUp,    pressed); };
   gp.dpad.down.valueChangedHandler  = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonDpadDown,  pressed); };
   gp.dpad.left.valueChangedHandler  = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonDpadLeft,  pressed); };
   gp.dpad.right.valueChangedHandler = ^(GCControllerButtonInput*, float, BOOL pressed) { gc_cpp_push_button(h, deviceId, kButtonDpadRight, pressed); };

   // GameController reports Y-up positive; SDL/VPX expects Y-down
   // positive. Flip so the axis-as-button thresholds in InputManager
   // read the same as they did from the SDL path.
   gp.leftThumbstick.xAxis.valueChangedHandler  = ^(GCControllerAxisInput*, float value) { gc_cpp_push_axis(h, deviceId, kAxisLeftX,   value); };
   gp.leftThumbstick.yAxis.valueChangedHandler  = ^(GCControllerAxisInput*, float value) { gc_cpp_push_axis(h, deviceId, kAxisLeftY,  -value); };
   gp.rightThumbstick.xAxis.valueChangedHandler = ^(GCControllerAxisInput*, float value) { gc_cpp_push_axis(h, deviceId, kAxisRightX,  value); };
   gp.rightThumbstick.yAxis.valueChangedHandler = ^(GCControllerAxisInput*, float value) { gc_cpp_push_axis(h, deviceId, kAxisRightY, -value); };

   // Triggers: GC reports 0..1, remap to -1..1 so the default mapping's
   // -0.3/0.3 thresholds (matched to SDL's full-range axes) work as-is.
   gp.leftTrigger.valueChangedHandler  = ^(GCControllerButtonInput*, float value, BOOL) { gc_cpp_push_axis(h, deviceId, kAxisLT, value * 2.0f - 1.0f); };
   gp.rightTrigger.valueChangedHandler = ^(GCControllerButtonInput*, float value, BOOL) { gc_cpp_push_axis(h, deviceId, kAxisRT, value * 2.0f - 1.0f); };
}


static void registerController(VPXGamepadState* state, GCController* controller)
{
   if ([state.deviceIds objectForKey:controller] != nil)
      return; // already registered

   if (controller.extendedGamepad == nil)
   {
      NSLog(@"[GCInputHandler] Ignoring non-extended controller: %@", controller.vendorName ?: @"unknown");
      return;
   }

   NSString* settingId    = makeSettingId(state, controller);
   NSString* friendlyName = controller.vendorName ?: controller.productCategory ?: @"Game Controller";

   const uint16_t deviceId = gc_cpp_register_device(state.handler, settingId.UTF8String, friendlyName.UTF8String);
   [state.deviceIds setObject:@(deviceId) forKey:controller];

   NSLog(@"[GCInputHandler] Connected: %@ (deviceId=%u, settingId=%@)", friendlyName, deviceId, settingId);

   // Names shown in VPX's in-game binding UI.
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonSouth,     "A / South");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonEast,      "B / East");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonWest,      "X / West");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonNorth,     "Y / North");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonLB,        "Left Shoulder");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonRB,        "Right Shoulder");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonView,      "View / Back");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonMenu,      "Menu / Start");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonHome,      "Home");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonL3,        "Left Stick Click");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonR3,        "Right Stick Click");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonDpadUp,    "DPad Up");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonDpadDown,  "DPad Down");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonDpadLeft,  "DPad Left");
   gc_cpp_register_element_name(state.handler, deviceId, 0, kButtonDpadRight, "DPad Right");
   gc_cpp_register_element_name(state.handler, deviceId, 1, kAxisLeftX,       "Left Stick X");
   gc_cpp_register_element_name(state.handler, deviceId, 1, kAxisLeftY,       "Left Stick Y");
   gc_cpp_register_element_name(state.handler, deviceId, 1, kAxisRightX,      "Right Stick X");
   gc_cpp_register_element_name(state.handler, deviceId, 1, kAxisRightY,      "Right Stick Y");
   gc_cpp_register_element_name(state.handler, deviceId, 1, kAxisLT,          "Left Trigger");
   gc_cpp_register_element_name(state.handler, deviceId, 1, kAxisRT,          "Right Trigger");

   gc_cpp_install_default_mapping(state.handler, deviceId);
   installValueHandlers(state, controller, deviceId);
}


static void unregisterController(VPXGamepadState* state, GCController* controller)
{
   NSNumber* deviceIdBox = [state.deviceIds objectForKey:controller];
   if (deviceIdBox == nil)
      return;

   const uint16_t deviceId = (uint16_t)deviceIdBox.unsignedShortValue;
   NSLog(@"[GCInputHandler] Disconnected: deviceId=%u", deviceId);

   // Drop the value-changed blocks so reconnecting controllers don't
   // double-fire from stale blocks.
   if (GCExtendedGamepad* gp = controller.extendedGamepad)
   {
      for (GCControllerButtonInput* b in @[gp.buttonA, gp.buttonB, gp.buttonX, gp.buttonY,
                                            gp.leftShoulder, gp.rightShoulder,
                                            gp.leftTrigger, gp.rightTrigger,
                                            gp.buttonMenu])
         b.valueChangedHandler = nil;
   }

   gc_cpp_unregister_device(state.handler, deviceId);
   [state.deviceIds removeObjectForKey:controller];
}


extern "C" void gc_objc_install(GCInputHandlerRef handler)
{
   @autoreleasepool {
      if (g_states == nil)
         g_states = [NSMutableDictionary dictionary];

      NSValue* key = [NSValue valueWithPointer:handler];
      if (g_states[key] != nil)
         return; // already installed

      VPXGamepadState* state = [VPXGamepadState new];
      state.handler = handler;
      state.deviceIds = [NSMapTable strongToStrongObjectsMapTable];
      state.modelCounters = [NSMutableDictionary dictionary];
      g_states[key] = state;

      NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
      state.connectObserver = [nc addObserverForName:GCControllerDidConnectNotification
                                              object:nil
                                               queue:[NSOperationQueue mainQueue]
                                          usingBlock:^(NSNotification* note) {
         registerController(state, (GCController*)note.object);
      }];
      state.disconnectObserver = [nc addObserverForName:GCControllerDidDisconnectNotification
                                                 object:nil
                                                  queue:[NSOperationQueue mainQueue]
                                             usingBlock:^(NSNotification* note) {
         unregisterController(state, (GCController*)note.object);
      }];

      // Pick up controllers already connected at install time.
      for (GCController* controller in GCController.controllers)
         registerController(state, controller);
   }
}


extern "C" void gc_objc_uninstall(GCInputHandlerRef handler)
{
   @autoreleasepool {
      if (g_states == nil)
         return;
      NSValue* key = [NSValue valueWithPointer:handler];
      VPXGamepadState* state = g_states[key];
      if (state == nil)
         return;

      NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
      if (state.connectObserver)    [nc removeObserver:state.connectObserver];
      if (state.disconnectObserver) [nc removeObserver:state.disconnectObserver];

      // Unregister any still-connected controllers so InputManager
      // doesn't hold stale device records past our lifetime.
      NSArray<GCController*>* live = [[state.deviceIds keyEnumerator] allObjects];
      for (GCController* c in live)
         unregisterController(state, c);

      [g_states removeObjectForKey:key];
   }
}
