#include "picocom/devkit.h"
#include "sdl_input_driver.h"
#include "picocom/input/input.h"


// Globals
struct SDLInputDriverState_t* g_SDLDriverInputState = 0;


//
//
void hid_driver_init()
{
    if(g_SDLDriverInputState)
        return;
    
    g_SDLDriverInputState = picocom_malloc(sizeof(*g_SDLDriverInputState));
    memset(g_SDLDriverInputState, 0, sizeof(*g_SDLDriverInputState));

    // defaults
    g_SDLDriverInputState->mouseConnected = true;
    g_SDLDriverInputState->keyboardConnected = true;

    // default map (xbox)
    g_SDLDriverInputState->sdlToPicocomButtonMap[4] = GamepadButton_Select;  
    g_SDLDriverInputState->sdlToPicocomButtonMap[6] = GamepadButton_Start;  
    g_SDLDriverInputState->sdlToPicocomButtonMap[0] = GamepadButton_A;  
    g_SDLDriverInputState->sdlToPicocomButtonMap[1] = GamepadButton_B;    
    g_SDLDriverInputState->sdlToPicocomButtonMap[2] = GamepadButton_X;  
    g_SDLDriverInputState->sdlToPicocomButtonMap[3] = GamepadButton_Y;         
    g_SDLDriverInputState->sdlToPicocomButtonMap[9] = GamepadButton_LeftShoulder;       
    g_SDLDriverInputState->sdlToPicocomButtonMap[10] = GamepadButton_RightShoulder;       

    g_SDLDriverInputState->sdlToPicocomDpadMap[13] = GamepadDpad_Left;  
    g_SDLDriverInputState->sdlToPicocomDpadMap[14] = GamepadDpad_Right;  
    g_SDLDriverInputState->sdlToPicocomDpadMap[11] = GamepadDpad_Up;  
    g_SDLDriverInputState->sdlToPicocomDpadMap[12] = GamepadDpad_Down;  

    g_SDLDriverInputState->gamepadDefaultScale = 1.0f / 32767.0f;
    g_SDLDriverInputState->gamepadDefaultAxisDeadzone = 2500;    
}


static void scanGamepads()
{
    if(!g_SDLDriverInputState)
        return;

	if (g_SDLDriverInputState->gamepad) {
		if (!SDL_GameControllerGetAttached(g_SDLDriverInputState->gamepad))
		{
			SDL_GameControllerClose(g_SDLDriverInputState->gamepad);
			g_SDLDriverInputState->gamepad = 0;			
		}
	}

	if (SDL_NumJoysticks() < 1)
	{
		return;
	}
	else if (!g_SDLDriverInputState->gamepad)
	{                  
		// Get gamepad
		g_SDLDriverInputState->gamepad = SDL_GameControllerOpen(0);
		if (g_SDLDriverInputState->gamepad == NULL)
		{
            printf("Warning: Unable to open game controller! SDL Error: %s\n", SDL_GetError());
		}
	}
}


void hid_update_sdl_state()
{
    if(!g_SDLDriverInputState)
        return;


    struct HIDCMD_getState* HIDState = &g_SDLDriverInputState->HIDState;

    // copy sim policy state ( these are always connected on desktop sim )
    HIDState->mouseConnected = g_SDLDriverInputState->mouseConnected;
    HIDState->keyboardConnected = g_SDLDriverInputState->keyboardConnected;
    HIDState->gamepadConnected = g_SDLDriverInputState->gamepad != 0;

    // Poll sdl event
    SDL_Event event;
    while(SDL_PollEvent(&event)) 
    {
        switch (event.type) {
            case SDL_MOUSEMOTION:
            {   
                uint32_t simWindowScale = sdl_display_get_window_scale();

                // Update state
                g_SDLDriverInputState->mouseState.id++;
                g_SDLDriverInputState->mouseState.deltaX = event.motion.xrel / simWindowScale;
                g_SDLDriverInputState->mouseState.deltaY = event.motion.yrel / simWindowScale;
                g_SDLDriverInputState->mouseState.deltaScroll = 0;
                g_SDLDriverInputState->mouseState.mouseX =  event.motion.x / simWindowScale;
                g_SDLDriverInputState->mouseState.mouseY =  event.motion.y / simWindowScale;

                // use button state
                g_SDLDriverInputState->mouseState.buttons = g_SDLDriverInputState->bufferMouseButtons;
                
                // append hid state
                if(HIDState->mouseCnt >= INPUT_HID_MAX_MOUSE_BUFFER_CNT)
                {
                    HIDState->mouseOverflowCnt++;
                    return;
                }
                HIDState->mouse[HIDState->mouseCnt++] = g_SDLDriverInputState->mouseState;
                HIDState->mouseCnt = 1;
                HIDState->id++;

                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                bool isDown = event.type == SDL_MOUSEBUTTONDOWN;
                int bit = -1;
                switch(event.button.button)
                {
                    case SDL_BUTTON_LEFT:
                        bit = MouseButton_Left;
                        break;
                    case SDL_BUTTON_RIGHT:
                        bit = MouseButton_Right;
                        break;                        
                    case SDL_BUTTON_MIDDLE:
                        bit = MouseButton_Middle;
                        break;                                                
                }

                if(bit != -1)
                {
                    if(isDown)
                        g_SDLDriverInputState->bufferMouseButtons |= (  bit );
                    else
                        g_SDLDriverInputState->bufferMouseButtons &=  ( ~(bit) );
                }

                // use button state
                g_SDLDriverInputState->mouseState.buttons = g_SDLDriverInputState->bufferMouseButtons;

                // push event
                HIDState->mouse[HIDState->mouseCnt++] = g_SDLDriverInputState->mouseState;
                HIDState->mouseCnt = 1;
                HIDState->id++;

                break;
            }    
            case SDL_MOUSEWHEEL:
            {
                // Update state
                g_SDLDriverInputState->mouseState.id++;     
                g_SDLDriverInputState->mouseState.deltaX = 0;
                g_SDLDriverInputState->mouseState.deltaY = 0;                           
                g_SDLDriverInputState->mouseState.deltaScroll = event.wheel.y;

                // use button state
                g_SDLDriverInputState->mouseState.buttons = g_SDLDriverInputState->bufferMouseButtons;
                
                // append hid state
                if(HIDState->mouseCnt >= INPUT_HID_MAX_MOUSE_BUFFER_CNT)
                {
                    HIDState->mouseOverflowCnt++;
                    return;
                }
                HIDState->mouse[HIDState->mouseCnt++] = g_SDLDriverInputState->mouseState;
                HIDState->mouseCnt = 1;
                HIDState->id++;
                break;
            }
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                // handle key state change
                bool isDown = event.type == SDL_KEYDOWN;
                int scancode = event.key.keysym.scancode;
                if(scancode >= 0xff)
                    break;
                    
                
                // handle modifiers ( bit clunky )
                bool isModifier = false;
                uint8_t modifierBit = 0;
                switch(event.key.keysym.sym)
                {                     
                    case SDLK_LCTRL:                        
                        isModifier = true;
                        modifierBit = SDLHIDModifierKey_LCTRL;
                        break;
                    case SDLK_LSHIFT:
                        isModifier = true;
                        modifierBit = SDLHIDModifierKey_LSHIFT;
                        break;
                    case SDLK_LALT:
                        isModifier = true;
                        modifierBit = SDLHIDModifierKey_LALT;
                        break;
                    case SDLK_LGUI:
                        isModifier = true;
                        modifierBit = SDLHIDModifierKey_LMETA;
                        break;
                    case SDLK_RCTRL:
                        isModifier = true;
                        modifierBit = SDLHIDModifierKey_RCTRL;
                        break;
                    case SDLK_RSHIFT:
                        isModifier = true;
                        modifierBit = SDLHIDModifierKey_RSHIFT;
                        break;
                    case SDLK_RALT:
                        isModifier = true;
                        modifierBit = SDLHIDModifierKey_RALT;
                        break;
                    case SDLK_RGUI:
                        isModifier = true;
                        modifierBit = SDLHIDModifierKey_RMETA;
                        break;
                }

                if(isModifier)
                {
                    if(modifierBit != -1)
                    {
                        if(isDown)
                            g_SDLDriverInputState->modifierState |= (  modifierBit );
                        else
                            g_SDLDriverInputState->modifierState &=  ( ~(modifierBit) );
                    }
                }
                
                // allow modifier scan codes (?)
                // TODO: how does this work on HW, scancodes or separate modifier state or both ?
                if(!isModifier) 
                {
                    if(isDown) // add to report
                    {
                        // check if already set
                        bool alreadyExists = false;
                        for(int i=0;i<HID_KeyboardStateData_keycode_Max;i++)
                        {                        
                            if(g_SDLDriverInputState->keyboardState.keycode[i] == scancode)
                            {
                                alreadyExists = true;
                                g_SDLDriverInputState->sdlKeyStateTimes[i] = picocom_time_us_32();
                                break;
                            }
                        } 

                        if(alreadyExists)
                        {
                            break;
                        }

                        // find oldest or empty key slot
                        int foundIndex = -1;
                        uint32_t foundDt = 0;
                        for(int i=0;i<HID_KeyboardStateData_keycode_Max;i++)
                        {
                            uint32_t dt =  picocom_time_us_32() - g_SDLDriverInputState->sdlKeyStateTimes[i];

                            if(g_SDLDriverInputState->keyboardState.keycode[i] == 0)
                            {
                                foundIndex = i;
                                break;
                            }
                            else if(foundIndex == -1 || dt > foundDt)
                            {
                                foundIndex = i;
                                foundDt = dt;
                            }
                        }

                        if(foundIndex != -1)
                        {
                            g_SDLDriverInputState->keyboardState.keycode[foundIndex] = scancode;
                            g_SDLDriverInputState->sdlKeyStateTimes[foundIndex] = picocom_time_us_32();
                        }

                    }
                    else // remove key from report
                    {
                        for(int i=0;i<HID_KeyboardStateData_keycode_Max;i++)
                        {
                            if(g_SDLDriverInputState->keyboardState.keycode[i] == scancode)
                                g_SDLDriverInputState->keyboardState.keycode[i] = 0;
                        }
                    }
                }
                
                g_SDLDriverInputState->keyboardState.id++;  
                g_SDLDriverInputState->keyboardState.modifier = g_SDLDriverInputState->modifierState;

                // append hid state
                if(HIDState->keyboardCnt >= INPUT_HID_MAX_MOUSE_BUFFER_CNT)
                {
                    HIDState->keyboardOverflowCnt++;
                    return;
                }
                HIDState->keyboard[HIDState->keyboardCnt++] = g_SDLDriverInputState->keyboardState;
                HIDState->id++;

                break;
            }        
            case SDL_QUIT:
            {
                picocom_get_instance()->running = 0;
                exit(1);
                break;
            }   
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
            {
                scanGamepads();
                break;
            }               	
        }
    } 

	// Sample gamepad buttons
    if(g_SDLDriverInputState->gamepad)
    {      
        // Poll gamepad
        g_SDLDriverInputState->gamepadState.buttons = 0;
        g_SDLDriverInputState->gamepadState.dpad = 0;
		for (size_t i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)        
		{		
            int state = SDL_GameControllerGetButton(g_SDLDriverInputState->gamepad, (SDL_GameControllerButton)i);	           
            if(state != 0)
            {                                
		        g_SDLDriverInputState->gamepadState.buttons |= g_SDLDriverInputState->sdlToPicocomButtonMap[i];		
                g_SDLDriverInputState->gamepadState.dpad |= g_SDLDriverInputState->sdlToPicocomDpadMap[i];		
            }
		}


        // Poll axes
        memset(g_SDLDriverInputState->gamepadState.axes, 0, sizeof(g_SDLDriverInputState->gamepadState.axes));
        for (size_t i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
        {
            int16_t rawVal = SDL_GameControllerGetAxis(g_SDLDriverInputState->gamepad, (SDL_GameControllerAxis)i);
            if(abs(rawVal) > g_SDLDriverInputState->gamepadDefaultAxisDeadzone )
            {
                float val = rawVal * g_SDLDriverInputState->gamepadDefaultScale;                  
                g_SDLDriverInputState->gamepadState.axes[i] = val;
            }

        }

        if(memcmp(&g_SDLDriverInputState->lastState,&g_SDLDriverInputState->gamepadState,sizeof(g_SDLDriverInputState->lastState)) == 0)
            return;

        // append hid state
        if(HIDState->gamepadCnt >= INPUT_HID_MAX_MOUSE_BUFFER_CNT)
        {
            HIDState->gamepadOverflowCnt++;
            return;
        }
        HIDState->gamepad[HIDState->gamepadCnt++] = g_SDLDriverInputState->gamepadState;
        HIDState->id++;  

        g_SDLDriverInputState->lastState = g_SDLDriverInputState->gamepadState;        
    }
}


struct HIDCMD_getState* hid_driver_get_state()
{
    if(!g_SDLDriverInputState)
        return 0;
    return &g_SDLDriverInputState->HIDState;
}


void hid_driver_update()
{
    // HW runs HID update
}
