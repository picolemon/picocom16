#pragma once

/** Low level HID cmd interface. 
*/
#include "platform/pico/bus/bus.h"


// Config
#define INPUT_HID_MAX_MOUSE_BUFFER_CNT 8            // Max mouse buffer states
#define INPUT_HID_MAX_KEYBOARD_BUFFER_CNT 8         // Max keyboard buffer states
#define INPUT_HID_MAX_GAMEPAD_BUFFER_CNT 8          // Max gamepad buffer states


/** Mouse delta state */
typedef struct __attribute__((__packed__))HID_MouseStateData
{
    uint32_t id;    // unique state id, incremented every update
    uint8_t buttons; // Mouse buttons state ( EMouseButton )
    int8_t  deltaX;  // X delta
    int8_t  deltaY;  // Y delta
    int8_t  deltaScroll;  // Scroll delta    
#ifdef PICOCOM_SDL     
    int32_t mouseX; // Abs pos
    int32_t mouseY;
#endif
} HID_MouseStateData;


#define HID_KeyboardStateData_keycode_Max 6


/** Keyboard delta state */
typedef struct __attribute__((__packed__))HID_KeyboardStateData
{
    uint32_t id;        // unique state id, incremented every update  
    uint8_t modifier;   // Keyboard modifier    
    uint8_t keycode[HID_KeyboardStateData_keycode_Max]; // Keycodes pressed
} HID_KeyboardStateData;


/** Gamepad delta state */
#define HIDGamePadNumAxes 6
typedef struct __attribute__((__packed__))HID_GamepadStateData
{    
    uint32_t id;        // unique state id, incremented every update
    uint8_t buttons;    // Gamepad button states
    uint8_t dpad;       // D-pad button state
    float axes[HIDGamePadNumAxes];      // Axes states
} HID_GamepadStateData;


/** HID event */
typedef struct __attribute__((__packed__))HID_Event
{
    uint32_t id;    // Event id
    uint8_t eventType; // [EHIDEventType]    
    HID_MouseStateData mouse;
    HID_KeyboardStateData keyboard;
    HID_GamepadStateData gamepad;
} HID_Event;


/** HID state */
typedef struct __attribute__((__packed__)) HIDCMD_getState
{    
    Cmd_Header_t header;    
    uint32_t id;            // unique state id, incremented on change

    // mouse input buffer
    uint8_t mouseConnected;
    HID_MouseStateData mouse[INPUT_HID_MAX_MOUSE_BUFFER_CNT];
    uint8_t mouseCnt;
    uint32_t mouseOverflowCnt; 

    uint8_t keyboardConnected;
    HID_KeyboardStateData keyboard[INPUT_HID_MAX_KEYBOARD_BUFFER_CNT];
    uint8_t keyboardCnt;
    uint8_t keyboardOverflowCnt;

    uint8_t gamepadConnected;
    HID_GamepadStateData gamepad[INPUT_HID_MAX_GAMEPAD_BUFFER_CNT];
    uint8_t gamepadCnt;
    uint8_t gamepadOverflowCnt;

} HIDCMD_getState;

