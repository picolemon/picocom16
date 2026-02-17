/** HID glue for sdl sim
 */
#pragma once

#include "picocom/platform.h"
#include "platform/pico/input/hw_hid_types.h"
#include "platform/sdl2/display/sdl_display_driver.h"


/** HID profile modifier keys */
enum SDLHIDModifierKey
{
    SDLHIDModifierKey_LCTRL =  0x01,
    SDLHIDModifierKey_LSHIFT = 0x02,
    SDLHIDModifierKey_LALT =   0x04,
    SDLHIDModifierKey_LMETA =  0x08,
    SDLHIDModifierKey_RCTRL =  0x10,
    SDLHIDModifierKey_RSHIFT = 0x20,
    SDLHIDModifierKey_RALT =   0x40,
    SDLHIDModifierKey_RMETA =  0x80,
};


/** SDL display driver 
*/
typedef struct SDLInputDriverState_t
{    
    uint8_t mouseConnected;
    uint8_t keyboardConnected;
    SDL_GameController* gamepad;
    int sdlToPicocomButtonMap[SDL_CONTROLLER_BUTTON_MAX];           // Map sdl buttons to picocom
    int sdlToPicocomDpadMap[SDL_CONTROLLER_BUTTON_MAX];             // Map sdl buttons to picocom    
    HID_GamepadStateData lastState;
    HID_GamepadStateData gamepadState;  // current gamepad state
    HID_MouseStateData mouseState;      // current mouse state
    HID_KeyboardStateData keyboardState; // current keyboard state
    uint8_t bufferMouseButtons;    
    uint8_t modifierState;
    struct HIDCMD_getState HIDState;    
    uint32_t sdlKeyStateTimes[6];       // key times in simulated hid report
    float gamepadDefaultScale;
    int32_t gamepadDefaultAxisDeadzone;        
} SDLInputDriverState_t;


// hid driver
void hid_driver_init();
struct HIDCMD_getState* hid_driver_get_state(); 
