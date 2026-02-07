#pragma once

#include "picocom/platform.h"   
#include "platform/pico/input/hw_hid_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Config
#ifndef INPUT_DEFAULT_GAMEPAD_DEADZONE
    #define INPUT_DEFAULT_GAMEPAD_DEADZONE 0.1          // Default gamepad stick deadzone for all axes
#endif    


// Fwd
struct ApuClientImpl_t;


/** HID event type */
enum EHIDEventType
{
    EHIDEventType_None,
    EHIDEventType_Mouse,
    EHIDEventType_Keyboard,
    EHIDEventType_Gamepad,
};


/** Gamepad buttons */
enum GamepadButton 
{
    GamepadButton_A = 1 << 0,
    GamepadButton_B = 1 << 1,
    GamepadButton_X = 1 << 2,
    GamepadButton_Y = 1 << 3,   
    GamepadButton_Select = 1 << 4,
    GamepadButton_Start = 1 << 5,    
    GamepadButton_LeftShoulder = 1 << 6,    
    GamepadButton_RightShoulder = 1 << 7,    
};


/** Gamepad dpad */
enum GamepadDpad 
{
    GamepadDpad_Left = 1 << 0,
    GamepadDpad_Right = 1 << 1,
    GamepadDpad_Up = 1 << 2,
    GamepadDpad_Down = 1 << 3,
};


/** Gamepad axis */
enum GamepadAxis 
{
    GamepadAxis_LeftStick_X = 0,
    GamepadAxis_LeftStick_Y,
    GamepadAxis_RightStick_X,
    GamepadAxis_RightStick_Y,    
    GamepadAxis_LeftTrigger,
    GamepadAxis_RightTrigger,
};


/** Keycode map */
enum KeyboardKeycode 
{
    KeyboardKeycode_NONE = 0x00, 
    KeyboardKeycode_ERR_OVF = 0x01,         //  Rollover
    KeyboardKeycode_A = 0x04,           // a and A
    KeyboardKeycode_B = 0x05,           // b and B
    KeyboardKeycode_C = 0x06,           // c and C
    KeyboardKeycode_D = 0x07,           // d and D
    KeyboardKeycode_E = 0x08,           // e and E
    KeyboardKeycode_F = 0x09,           // f and F
    KeyboardKeycode_G = 0x0a,           // g and G
    KeyboardKeycode_H = 0x0b,           // h and H
    KeyboardKeycode_I = 0x0c,           // i and I
    KeyboardKeycode_J = 0x0d,           // j and J
    KeyboardKeycode_K = 0x0e,           // k and K
    KeyboardKeycode_L = 0x0f,           // l and L
    KeyboardKeycode_M = 0x10,           // m and M
    KeyboardKeycode_N = 0x11,           // n and N
    KeyboardKeycode_O = 0x12,           // o and O
    KeyboardKeycode_P = 0x13,           // p and P
    KeyboardKeycode_Q = 0x14,           // q and Q
    KeyboardKeycode_R = 0x15,           // r and R
    KeyboardKeycode_S = 0x16,           // s and S
    KeyboardKeycode_T = 0x17,           // t and T
    KeyboardKeycode_U = 0x18,           // u and U
    KeyboardKeycode_V = 0x19,           // v and V
    KeyboardKeycode_W = 0x1a,           // w and W
    KeyboardKeycode_X = 0x1b,           // x and X
    KeyboardKeycode_Y = 0x1c,           // y and Y
    KeyboardKeycode_Z = 0x1d,           // z and Z
    KeyboardKeycode_1 = 0x1e,           // 1 and !
    KeyboardKeycode_2 = 0x1f,           // 2 and @
    KeyboardKeycode_3 = 0x20,           // 3 and #
    KeyboardKeycode_4 = 0x21,           // 4 and $
    KeyboardKeycode_5 = 0x22,           // 5 and %
    KeyboardKeycode_6 = 0x23,           // 6 and ^
    KeyboardKeycode_7 = 0x24,           // 7 and &
    KeyboardKeycode_8 = 0x25,           // 8 and *
    KeyboardKeycode_9 = 0x26,           // 9 and (
    KeyboardKeycode_0 = 0x27,           // 0 and )
    KeyboardKeycode_Enter = 0x28,       // Return
    KeyboardKeycode_Escape = 0x29,      // Escape
    KeyboardKeycode_Backspace = 0x2a,   // Backspace
    KeyboardKeycode_Tab = 0x2b,         // Tab
    KeyboardKeycode_Space = 0x2c,       // Space
    KeyboardKeycode_Minus = 0x2d,       // - and _
    KeyboardKeycode_Equal = 0x2e,       // = and +
    KeyboardKeycode_LeftBrace = 0x2f,   // [ and {
    KeyboardKeycode_RightBrace = 0x30,  // ] and }
    KeyboardKeycode_Backlash = 0x31,    // \ and |
    KeyboardKeycode_Hashtilde = 0x32,   // # and ~
    KeyboardKeycode_Semicolon = 0x33,   // ; and :
    KeyboardKeycode_Apostraphe = 0x34,  // ' and "
    KeyboardKeycode_Grave = 0x35,       // ` and ~
    KeyboardKeycode_Comma = 0x36,       // , and <
    KeyboardKeycode_Dot = 0x37,         // . and >
    KeyboardKeycode_Slash = 0x38,       // / and ?
    KeyboardKeycode_Capslock = 0x39,    // Caps Lock
    KeyboardKeycode_F1 = 0x3a,          // F1
    KeyboardKeycode_F2 = 0x3b,          // F2
    KeyboardKeycode_F3 = 0x3c,          // F3
    KeyboardKeycode_F4 = 0x3d,          // F4
    KeyboardKeycode_F5 = 0x3e,          // F5
    KeyboardKeycode_F6 = 0x3f,          // F6
    KeyboardKeycode_F7 = 0x40,          // F7
    KeyboardKeycode_F8 = 0x41,          // F8
    KeyboardKeycode_F9 = 0x42,          // F9
    KeyboardKeycode_F10 = 0x43,         // F10
    KeyboardKeycode_F11 = 0x44,         // F11
    KeyboardKeycode_F12 = 0x45,         // F12
    KeyboardKeycode_Sysrq = 0x46,       // Print Screen
    KeyboardKeycode_Scrollock = 0x47,   // Scroll Lock
    KeyboardKeycode_Pause = 0x48,       // Pause
    KeyboardKeycode_Insert = 0x49,      // Insert
    KeyboardKeycode_Home = 0x4a,        // Home
    KeyboardKeycode_PageUp = 0x4b,      // Page Up
    KeyboardKeycode_Delete = 0x4c,      // Delete Forward
    KeyboardKeycode_End = 0x4d,         // End
    KeyboardKeycode_PageDown = 0x4e,    // Page Down
    KeyboardKeycode_Right = 0x4f,       // Right Arrow
    KeyboardKeycode_Left = 0x50,        // Left Arrow
    KeyboardKeycode_Down = 0x51,        // Down Arrow
    KeyboardKeycode_Up = 0x52,          // Up Arrow
    KeyboardKeycode_NumLock = 0x53,     // Num Lock and Clear
    KeyboardKeycode_KeyPadSlash = 0x54, // Keypad /
    KeyboardKeycode_KeyPadAsterisk = 0x5,// Keypad *
    KeyboardKeycode_KeyPadMinus = 0x56, // Keypad -
    KeyboardKeycode_KeyPadPlus = 0x57,  // Keypad +
    KeyboardKeycode_KeyPadEnter = 0x58, // Keypad ENTER
    KeyboardKeycode_KeyPad1 = 0x59,     // Keypad 1 and End
    KeyboardKeycode_KeyPad2 = 0x5a,     // Keypad 2 and Down Arrow
    KeyboardKeycode_KeyPad3 = 0x5b,     // Keypad 3 and PageDn
    KeyboardKeycode_KeyPad4 = 0x5c,     // Keypad 4 and Left Arrow
    KeyboardKeycode_KeyPad5 = 0x5d,     // Keypad 5
    KeyboardKeycode_KeyPad6 = 0x5e,     // Keypad 6 and Right Arrow
    KeyboardKeycode_KeyPad7 = 0x5f,     // Keypad 7 and Home
    KeyboardKeycode_KeyPad8 = 0x60,     // Keypad 8 and Up Arrow
    KeyboardKeycode_KeyPad9 = 0x61,     // Keypad 9 and Page Up
    KeyboardKeycode_KeyPad0 = 0x62,     // Keypad 0 and Insert
    KeyboardKeycode_KeyPadDot = 0x63,   // Keypad . and Delete
    KeyboardKeycode_102nd = 0x64,       // Non-US \ and |
    KeyboardKeycode_Compose = 0x65,     // Application
    KeyboardKeycode_Power = 0x66,       // Power
    KeyboardKeycode_KeyPadEqual = 0x67, // Keypad =
    KeyboardKeycode_F13 = 0x68,         // F13
    KeyboardKeycode_F14 = 0x69,         // F14
    KeyboardKeycode_F15 = 0x6a,         // F15
    KeyboardKeycode_F16 = 0x6b,         // F16
    KeyboardKeycode_F17 = 0x6c,         // F17
    KeyboardKeycode_F18 = 0x6d,         // F18
    KeyboardKeycode_F19 = 0x6e,         // F19
    KeyboardKeycode_F20 = 0x6f,         // F20
    KeyboardKeycode_F21 = 0x70,         // F21
    KeyboardKeycode_F22 = 0x71,         // F22
    KeyboardKeycode_F23 = 0x72,         // F23
    KeyboardKeycode_F24 = 0x73,         // F24
    KeyboardKeycode_Open = 0x74,        // Execute
    KeyboardKeycode_Help = 0x75,        // Help
    KeyboardKeycode_Props = 0x76,       // Menu
    KeyboardKeycode_Front = 0x77,       // Select
    KeyboardKeycode_Stop = 0x78,        // Stop
    KeyboardKeycode_Again = 0x79,       // Again
    KeyboardKeycode_Undo = 0x7a,        // Undo
    KeyboardKeycode_Cut = 0x7b,         // Cut
    KeyboardKeycode_Copy = 0x7c,        // Copy
    KeyboardKeycode_Paste = 0x7d,       // Paste
    KeyboardKeycode_Find = 0x7e,        // Find
    KeyboardKeycode_Mute = 0x7f,        // Mute
    KeyboardKeycode_VolumeUp = 0x80,    // Volume Up
    KeyboardKeycode_VolumeDown = 0x81,  // Volume Down
    KeyboardKeycode_KeyPadComma = 0x85, // Keypad Comma
    KeyboardKeycode_KeyPadLeftPren = 0xb6, // Keypad (
    KeyboardKeycode_KeyPadRightParam = 0xb7, // Keypad )
    KeyboardKeycode_LeftCtrl = 0xe0,    // Left Control
    KeyboardKeycode_LeftShift = 0xe1,   // Left Shift
    KeyboardKeycode_LeftAlt = 0xe2,     // Left Alt
    KeyboardKeycode_LeftMeta = 0xe3,    // Left GUI
    KeyboardKeycode_RightCtrl = 0xe4,   // Right Control
    KeyboardKeycode_RightShift = 0xe5,  // Right Shift
    KeyboardKeycode_RightAlt = 0xe6,    // Right Alt
    KeyboardKeycode_RightMeta = 0xe7,   // Right GUI
    KeyboardKeycode_MediaPlayPause = 0xe8,
    KeyboardKeycode_MediaStopCD = 0xe9,
    KeyboardKeycode_MediaPrevSong = 0xea,
    KeyboardKeycode_MediaNextSong = 0xeb,
    KeyboardKeycode_MediaEjectCD = 0xec,
    KeyboardKeycode_MediaVolUp = 0xed,
    KeyboardKeycode_MediaVolDown = 0xee,
    KeyboardKeycode_MediaMute = 0xef,
    KeyboardKeycode_MediaWWW = 0xf0,
    KeyboardKeycode_MediaBack = 0xf1,
    KeyboardKeycode_MediaForward = 0xf2,
    KeyboardKeycode_MediaStop = 0xf3,
    KeyboardKeycode_MediaFind = 0xf4,
    KeyboardKeycode_MediaScrollUp = 0xf5,
    KeyboardKeycode_MediaScrollDown = 0xf6,
    KeyboardKeycode_MediaEdit = 0xf7,
    KeyboardKeycode_MediaSleep = 0xf8,
    KeyboardKeycode_MediaCoffee = 0xf9,
    KeyboardKeycode_MediaRefresh = 0xfa,
    KeyboardKeycode_MediaCalc = 0xfb,    
    KeyboardKeycode_Max
};


/** Mouse buttons */
enum MouseButton
{
    MouseButton_Left = 1 << 0,
    MouseButton_Right = 1 << 1,
    MouseButton_Middle = 1 << 2,
};


/** Keyboard modifiers */
enum InputKeyboardModifier
{
  InputKeyboardModifier__LEFTCTRL   = 1 << 0,
  InputKeyboardModifier__LEFTSHIFT  = 1 << 1,
  InputKeyboardModifier__LEFTALT    = 1 << 2,
  InputKeyboardModifier__LEFTGUI    = 1 << 3,
  InputKeyboardModifier__RIGHTCTRL  = 1 << 4,
  InputKeyboardModifier__RIGHTSHIFT = 1 << 5,
  InputKeyboardModifier__RIGHTALT   = 1 << 6,
  InputKeyboardModifier__RIGHTGUI   = 1 << 7,
};


/** Input system init options
*/
typedef struct InputInitOptions_t {
    int enabled;
    struct ApuClientImpl_t* client;  
} InputInitOptions_t;


/** Input state */
typedef struct InputState_t
{	
    uint32_t timestamp;
    struct BusTx_t* apuLink_tx;
    struct BusRx_t* apuLink_rx;    
	struct ApuClientImpl_t* client;	
    struct HIDCMD_getState lastHIDState;
    struct HIDCMD_getState nextHIDState;
    bool hasNextState;

    // HID state    
    int hidEnabled;
    queue_t hidQueue;
    uint32_t lastHIDId;
    uint32_t hidOverflowCnt;            
    uint8_t bufferMouseButtons;
    uint8_t prevMouseButtons;
    struct HID_GamepadStateData lastGamepadState;       // gamepad single state based
    struct HID_GamepadStateData prevFrameGamepadState;  // prev gamepad inputs for up edge detection
    struct HID_MouseStateData lastMouseState;           // mouse state
    struct HID_KeyboardStateData lastKeyboardState;
    struct HID_KeyboardStateData prevFrameKeyboardState;
    int32_t mouseScreenX;      // abs mouse coord
    int32_t mouseScreenY;      // abs mouse coord    
    int mouseDeltaX;
    int mouseDeltaY;
    int mouseDeltaScroll;
    float gamepadDeadzone;       // Global deadzone
} InputState_t;


// Internal api
void input_post_hid_state(struct HIDCMD_getState* hidState);    // HID from apu

// Input api
struct InputInitOptions_t input_default_options(struct ApuClientImpl_t* apuClient);  // Get input default options
int input_init_with_options(struct InputInitOptions_t* options); // Init input api
int input_deinit();
int input_has_mouse();
int input_has_keyboard();
int input_has_gamepad();
void input_post_event(struct HID_Event* evt); // [internal] post input event to queue, posted from HID chip for input system states/queue.
int input_update();        // Frame update, clear prev input states for eg. key up states
int input_get_hit_state_blocking(bool clearHIDCounters, struct HIDCMD_getState* hidStateOut);   // Direct HID query to APU

// mouse api
int input_mouse_is_down(uint8_t buttonId);
int input_mouse_is_up(uint8_t buttonId);
int input_mouse_get_delta_x();
int input_mouse_get_delta_y();
int input_mouse_get_screen_x();
int input_mouse_get_screen_y();
int input_mouse_get_delta_scroll();
uint32_t input_get_state_time();

// keyboard api
int input_keyboard_is_keycode_down(uint8_t keycode);
int input_keyboard_is_keycode_up(uint8_t keycode);
char input_keycode_to_char(uint8_t keycode, uint8_t modifier);
uint8_t input_keyboard_modifier_keys();

// gamepad api
int input_gamepad_button_is_down(uint8_t buttonId);
uint8_t input_gamepad_buttons();
int input_gamepad_button_is_up(uint8_t buttonId);
int input_gamepad_dpad_is_down(uint8_t buttonId);
uint8_t input_gamepad_dpad();
int input_gamepad_dpad_is_up(uint8_t buttonId);
float input_gamepad_get_axis(uint8_t axisId);
void input_gamepad_set_deadzone( float deadZone );
float input_gamepad_get_deadzone( );

// event api
int input_event_count();      // Event pending in queue
struct HID_Event* input_event_pop();  // Pop event

#ifdef __cplusplus
}
#endif
