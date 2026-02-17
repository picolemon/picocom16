#include "input.h"
#include "picocom/devkit.h"      // DEP: input runs on APU
#include "picocom/input/input.h"      // DEP: input runs on APU
#include "picocom/display/display.h"    // For abs mouse coords
#include "components/apu_core/apu_client.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
#ifdef PICOCOM_SDL
#include "platform/sdl2/display/sdl_display_driver.h" 
#endif


// Fwd
void input_post_event(struct HID_Event* evt);
void test_service_input_main(void* userData);
struct HIDCMD_getState* hid_driver_get_state();


// Globals
struct InputState_t* g_InputState = 0;


// TinyUSB HID keycode defs
#define HID_KEYCODE_TO_ASCII    \
    {0     , 0      }, /* 0x00 */ \
    {0     , 0      }, /* 0x01 */ \
    {0     , 0      }, /* 0x02 */ \
    {0     , 0      }, /* 0x03 */ \
    {'a'   , 'A'    }, /* 0x04 */ \
    {'b'   , 'B'    }, /* 0x05 */ \
    {'c'   , 'C'    }, /* 0x06 */ \
    {'d'   , 'D'    }, /* 0x07 */ \
    {'e'   , 'E'    }, /* 0x08 */ \
    {'f'   , 'F'    }, /* 0x09 */ \
    {'g'   , 'G'    }, /* 0x0a */ \
    {'h'   , 'H'    }, /* 0x0b */ \
    {'i'   , 'I'    }, /* 0x0c */ \
    {'j'   , 'J'    }, /* 0x0d */ \
    {'k'   , 'K'    }, /* 0x0e */ \
    {'l'   , 'L'    }, /* 0x0f */ \
    {'m'   , 'M'    }, /* 0x10 */ \
    {'n'   , 'N'    }, /* 0x11 */ \
    {'o'   , 'O'    }, /* 0x12 */ \
    {'p'   , 'P'    }, /* 0x13 */ \
    {'q'   , 'Q'    }, /* 0x14 */ \
    {'r'   , 'R'    }, /* 0x15 */ \
    {'s'   , 'S'    }, /* 0x16 */ \
    {'t'   , 'T'    }, /* 0x17 */ \
    {'u'   , 'U'    }, /* 0x18 */ \
    {'v'   , 'V'    }, /* 0x19 */ \
    {'w'   , 'W'    }, /* 0x1a */ \
    {'x'   , 'X'    }, /* 0x1b */ \
    {'y'   , 'Y'    }, /* 0x1c */ \
    {'z'   , 'Z'    }, /* 0x1d */ \
    {'1'   , '!'    }, /* 0x1e */ \
    {'2'   , '@'    }, /* 0x1f */ \
    {'3'   , '#'    }, /* 0x20 */ \
    {'4'   , '$'    }, /* 0x21 */ \
    {'5'   , '%'    }, /* 0x22 */ \
    {'6'   , '^'    }, /* 0x23 */ \
    {'7'   , '&'    }, /* 0x24 */ \
    {'8'   , '*'    }, /* 0x25 */ \
    {'9'   , '('    }, /* 0x26 */ \
    {'0'   , ')'    }, /* 0x27 */ \
    {'\r'  , '\r'   }, /* 0x28 */ \
    {'\x1b', '\x1b' }, /* 0x29 */ \
    {'\b'  , '\b'   }, /* 0x2a */ \
    {'\t'  , '\t'   }, /* 0x2b */ \
    {' '   , ' '    }, /* 0x2c */ \
    {'-'   , '_'    }, /* 0x2d */ \
    {'='   , '+'    }, /* 0x2e */ \
    {'['   , '{'    }, /* 0x2f */ \
    {']'   , '}'    }, /* 0x30 */ \
    {'\\'  , '|'    }, /* 0x31 */ \
    {'#'   , '~'    }, /* 0x32 */ \
    {';'   , ':'    }, /* 0x33 */ \
    {'\''  , '\"'   }, /* 0x34 */ \
    {'`'   , '~'    }, /* 0x35 */ \
    {','   , '<'    }, /* 0x36 */ \
    {'.'   , '>'    }, /* 0x37 */ \
    {'/'   , '?'    }, /* 0x38 */ \
                                  \
    {0     , 0      }, /* 0x39 */ \
    {0     , 0      }, /* 0x3a */ \
    {0     , 0      }, /* 0x3b */ \
    {0     , 0      }, /* 0x3c */ \
    {0     , 0      }, /* 0x3d */ \
    {0     , 0      }, /* 0x3e */ \
    {0     , 0      }, /* 0x3f */ \
    {0     , 0      }, /* 0x40 */ \
    {0     , 0      }, /* 0x41 */ \
    {0     , 0      }, /* 0x42 */ \
    {0     , 0      }, /* 0x43 */ \
    {0     , 0      }, /* 0x44 */ \
    {0     , 0      }, /* 0x45 */ \
    {0     , 0      }, /* 0x46 */ \
    {0     , 0      }, /* 0x47 */ \
    {0     , 0      }, /* 0x48 */ \
    {0     , 0      }, /* 0x49 */ \
    {0     , 0      }, /* 0x4a */ \
    {0     , 0      }, /* 0x4b */ \
    {0     , 0      }, /* 0x4c */ \
    {0     , 0      }, /* 0x4d */ \
    {0     , 0      }, /* 0x4e */ \
    {0     , 0      }, /* 0x4f */ \
    {0     , 0      }, /* 0x50 */ \
    {0     , 0      }, /* 0x51 */ \
    {0     , 0      }, /* 0x52 */ \
    {0     , 0      }, /* 0x53 */ \
                                  \
    {'/'   , '/'    }, /* 0x54 */ \
    {'*'   , '*'    }, /* 0x55 */ \
    {'-'   , '-'    }, /* 0x56 */ \
    {'+'   , '+'    }, /* 0x57 */ \
    {'\r'  , '\r'   }, /* 0x58 */ \
    {'1'   , 0      }, /* 0x59 */ \
    {'2'   , 0      }, /* 0x5a */ \
    {'3'   , 0      }, /* 0x5b */ \
    {'4'   , 0      }, /* 0x5c */ \
    {'5'   , '5'    }, /* 0x5d */ \
    {'6'   , 0      }, /* 0x5e */ \
    {'7'   , 0      }, /* 0x5f */ \
    {'8'   , 0      }, /* 0x60 */ \
    {'9'   , 0      }, /* 0x61 */ \
    {'0'   , 0      }, /* 0x62 */ \
    {'.'   , 0      }, /* 0x63 */ \
    {0     , 0      }, /* 0x64 */ \
    {0     , 0      }, /* 0x65 */ \
    {0     , 0      }, /* 0x66 */ \
    {'='   , '='    }, /* 0x67 */ \

static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };


//
//
struct InputInitOptions_t input_default_options(struct ApuClientImpl_t* apuClient)
{
    struct InputInitOptions_t options = (struct InputInitOptions_t){
        .enabled = true,
        .client = apuClient,
    };
    return options;
}


int input_init_with_options(struct InputInitOptions_t* options)
{
    if(g_InputState)
        return SDKErr_Fail;
    
    if(!options->enabled)
        return SDKErr_OK;

    g_InputState = (struct InputState_t*)picocom_malloc(sizeof(struct InputState_t));
    memset(g_InputState, 0, sizeof(InputState_t));

    g_InputState->client = options->client;
    g_InputState->apuLink_tx = options->client->apuLink_tx;
    g_InputState->apuLink_rx = options->client->apuLink_rx;
    g_InputState->gamepadDeadzone = INPUT_DEFAULT_GAMEPAD_DEADZONE;

    // Init input queue 
    queue_init(&g_InputState->hidQueue, sizeof(HID_Event), 32);
    g_InputState->hidEnabled = 1;

    return SDKErr_OK;
}


int input_deinit()
{
    if(!g_InputState)
    {
        picocom_free(g_InputState);
        g_InputState = 0;
    }
    return SDKErr_OK;
}


int input_has_mouse()
{
    if(!g_InputState)
        return 0;
    
    return g_InputState->lastHIDState.mouseConnected;
}


int input_has_keyboard()
{
    if(!g_InputState)
        return 0;
    
    return g_InputState->lastHIDState.keyboardConnected;
}


int input_has_gamepad()
{
    if(!g_InputState)
        return 0;
    
    return g_InputState->lastHIDState.gamepadConnected;
}


void input_post_event(struct HID_Event* evt)
{
    if(!g_InputState)
        return;

    // Try queue
    if(!queue_try_add(&g_InputState->hidQueue, evt))
        g_InputState->hidOverflowCnt++;

    // Handle input state
    switch (evt->eventType) 
    {
        case EHIDEventType_Mouse:
        {
            struct HID_MouseStateData* mouse = &evt->mouse;
            g_InputState->bufferMouseButtons = mouse->buttons;

#ifdef PICOCOM_SDL 
            g_InputState->mouseScreenX = mouse->mouseX;
            g_InputState->mouseScreenY = mouse->mouseY;
#else
            g_InputState->mouseScreenX += mouse->deltaX;
            g_InputState->mouseScreenY += mouse->deltaY;

            if(g_InputState->mouseScreenX < 0)
                g_InputState->mouseScreenX = 0;
            if(g_InputState->mouseScreenY < 0)
                g_InputState->mouseScreenY = 0;            
            if(g_InputState->mouseScreenX >= FRAME_W)
                g_InputState->mouseScreenX = FRAME_W - 1;
            if(g_InputState->mouseScreenY >= FRAME_H)
                g_InputState->mouseScreenY = FRAME_H - 1; 
#endif
            // copy state
            g_InputState->lastMouseState = *mouse;

            g_InputState->mouseDeltaX += mouse->deltaX;
            g_InputState->mouseDeltaY += mouse->deltaY;
            g_InputState->mouseDeltaScroll += mouse->deltaScroll;
            g_InputState->timestamp = picocom_time_ms_32();
            
            break;
        }
        case EHIDEventType_Keyboard:
        {
            struct HID_KeyboardStateData* kb = &evt->keyboard;

            // copy state
            g_InputState->lastKeyboardState = *kb;

            break;                    
        }
        case EHIDEventType_Gamepad:
        {
            struct HID_GamepadStateData* gamepad = &evt->gamepad;

            // copy state
            g_InputState->lastGamepadState = *gamepad;
            break;
        }
    }   
}


int input_get_hit_state_blocking(bool clearHIDCounters, struct HIDCMD_getState* hidStateOut)
{
    if(!hidStateOut)
        return SDKErr_Fail;

    // query apu status
    struct Cmd_APU_HIDState cmd;       
    cmd.clearHIDCounters = clearHIDCounters;
    BUS_INIT_CMD(cmd, EBusCmd_APU_GetHIDState);                 
    struct Res_APU_HIDState response;
    int res = bus_tx_request_blocking_ex(g_InputState->apuLink_tx, g_InputState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_InputState->client->defaultTimeout, test_service_input_main, 0);

    // get file handle
    if(res != SDKErr_OK)
        return res;

    if(response.result != SDKErr_OK)
        return SDKErr_Fail;

    *hidStateOut = response.hidState;
    return SDKErr_OK;
}


int input_mouse_is_down(uint8_t buttonId)
{
    if(!g_InputState)
        return 0;    

    return g_InputState->bufferMouseButtons & buttonId;
}


int input_mouse_is_up(uint8_t buttonId)
{
    if(!g_InputState)
        return 0;    

    if(g_InputState->bufferMouseButtons & buttonId)
        return 0;

    return g_InputState->prevMouseButtons & buttonId;
}


int input_mouse_get_delta_x()
{
    if(!g_InputState)
        return 0;    
    return g_InputState->mouseDeltaX;
}


int input_mouse_get_delta_y()
{
    if(!g_InputState)
        return 0;    
    return g_InputState->mouseDeltaY;
}


int input_mouse_get_delta_scroll()
{
    if(!g_InputState)
        return 0;    
    return g_InputState->mouseDeltaScroll; 
}


int input_mouse_get_screen_x()
{
    if(!g_InputState)
        return 0;    
    return g_InputState->mouseScreenX;
}


uint32_t input_get_state_time()
{
    return g_InputState->timestamp;
}


int input_mouse_get_screen_y()
{
    if(!g_InputState)
        return 0;    
    return g_InputState->mouseScreenY;
}


// keyboard api


int input_keyboard_is_keycode_down(uint8_t keycode)
{
    if(!g_InputState)
        return 0;

    if(!keycode)
        return 0;

    for(int i=0;i<HID_KeyboardStateData_keycode_Max;i++)
    {
        if(g_InputState->lastKeyboardState.keycode[i] == keycode)
            return 1;
    }    
    return 0;    
}


int input_keyboard_is_keycode_up(uint8_t keycode)
{
    if(!g_InputState)
        return 0;

    if(!keycode)
        return 0;
    
    if(input_keyboard_is_keycode_down(keycode))
        return 0; // Still down

    // Find prev down
    for(int i=0;i<HID_KeyboardStateData_keycode_Max;i++)
    {
        if(g_InputState->prevFrameKeyboardState.keycode[i] == keycode)
            return 1;
    }    

    return 0;   
}


char input_keycode_to_char(uint8_t keycode, uint8_t modifier)
{
    bool const is_shift = modifier & (InputKeyboardModifier__LEFTSHIFT | InputKeyboardModifier__RIGHTSHIFT);
    return keycode2ascii[keycode][is_shift ? 1 : 0];    
}


uint8_t input_keyboard_modifier_keys()
{
    if(!g_InputState)
        return 0;

    return g_InputState->lastHIDState.keyboard->modifier;
}


int input_gamepad_button_is_down(uint8_t buttonId)
{
    if(!g_InputState)
        return 0;    
    return g_InputState->lastGamepadState.buttons & buttonId;
}


int input_gamepad_button_is_up(uint8_t buttonId)
{
    if(!g_InputState)
        return 0;    
    if(input_gamepad_button_is_down(buttonId))
        return 0;
    return g_InputState->prevFrameGamepadState.buttons & buttonId;
}

int input_gamepad_dpad_is_down(uint8_t buttonId)
{
    if(!g_InputState)
        return 0;    
    return g_InputState->lastGamepadState.dpad & buttonId;
}

uint8_t input_gamepad_buttons()
{
    if(!g_InputState)
        return 0;    
    return g_InputState->lastGamepadState.buttons;
}


uint8_t input_gamepad_dpad()
{
    if(!g_InputState)
        return 0;    
    return g_InputState->lastGamepadState.dpad;
}



int input_gamepad_dpad_is_up(uint8_t buttonId)
{
    if(!g_InputState)
        return 0;    
    if(input_gamepad_dpad_is_down(buttonId))
        return 0;
    return g_InputState->prevFrameGamepadState.dpad & buttonId;
}


float input_gamepad_get_axis(uint8_t axisId)
{
    if( axisId >= HIDGamePadNumAxes)
        return 0;
    float value = g_InputState->prevFrameGamepadState.axes[axisId];

    if(fabs(value) < g_InputState->gamepadDeadzone)
        value = 0;

    return value;
}


int input_event_count()
{
    if(!g_InputState)
        return 0;
    return queue_get_level(&g_InputState->hidQueue);
}


struct HID_Event* input_event_pop()
{
    if(!g_InputState)
        return 0;    
    static HID_Event result = {};
    if(queue_try_remove(&g_InputState->hidQueue, &result))
    {
        return &result;
    }
    return 0;
}


void input_post_hid_state(struct HIDCMD_getState* hidState)
{
    if(!hidState || !g_InputState)
        return;
        
    g_InputState->hasNextState = true;
    g_InputState->nextHIDState = *hidState;
}



int input_update()
{
    if(!g_InputState)
        return SDKErr_Fail;    

    // Edge detect between updates ( eg. _up triggers )
    memcpy(&g_InputState->prevFrameKeyboardState, &g_InputState->lastKeyboardState, sizeof(g_InputState->prevFrameKeyboardState));     // current game -> prev
    memcpy(&g_InputState->prevFrameGamepadState, &g_InputState->lastGamepadState, sizeof(g_InputState->prevFrameGamepadState));     // current game -> prev
    g_InputState->prevMouseButtons = g_InputState->bufferMouseButtons;

    g_InputState->mouseDeltaX = 0;
    g_InputState->mouseDeltaY = 0;
    g_InputState->mouseDeltaScroll = 0;


#ifdef PICOCOM_NATIVE_SIM  
    struct HIDCMD_getState* hidState = hid_driver_get_state();
    if( hidState)
    {
        memcpy( &g_InputState->nextHIDState, hidState, sizeof(struct HIDCMD_getState) );

        hidState->mouseCnt = 0;
        hidState->keyboardCnt = 0;
        hidState->gamepadCnt = 0;

        g_InputState->hasNextState = true;
    }

#endif        

    if(g_InputState->hasNextState)
    {
        g_InputState->hasNextState = false;
        struct HIDCMD_getState* hidState = &g_InputState->nextHIDState;

        for(int i=0;i<hidState->mouseCnt;i++)
        {
            HID_Event evt = {};
            evt.id = g_InputState->lastHIDId++;
            evt.eventType = EHIDEventType_Mouse;
            evt.mouse = hidState->mouse[i];
            input_post_event(&evt);
        }

        for(int i=0;i<hidState->keyboardCnt;i++)
        {            
            HID_Event evt = {};
            evt.id = g_InputState->lastHIDId++;
            evt.eventType = EHIDEventType_Keyboard;
            evt.keyboard = hidState->keyboard[i];
            input_post_event(&evt);
        }

        for(int i=0;i<hidState->gamepadCnt;i++)
        {
            HID_Event evt = {};
            evt.id = g_InputState->lastHIDId++;
            evt.eventType = EHIDEventType_Gamepad;
            evt.gamepad = hidState->gamepad[i];
            input_post_event(&evt);
        }

        g_InputState->lastHIDState = *hidState;     
    }

    return SDKErr_OK;
}


void input_gamepad_set_deadzone( float deadZone )
{
    if(!g_InputState)
        return;
    g_InputState->gamepadDeadzone = deadZone;
}


float input_gamepad_get_deadzone( )
{
    if(!g_InputState)
        return 0.0f;
    return g_InputState->gamepadDeadzone;
}