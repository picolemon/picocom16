#include "picocom/devkit.h"
#include "picocom/display/gfx.h"
#include "picocom/input/input.h"
#define IMPL_EMBED_fonts
#include "resources/fonts.inl"


//
//
void app_main()
{    
    PicocomInitOptions_t defaultOptions = picocom_get_default_init_options("gfx_test");
    defaultOptions.resetDevices = 1;
    picocom_init_with_options(&defaultOptions);    
   
    gfx_init();    
    display_reset_gpu();

    gfx_mount_asset_pack(&asset_fonts);   

    // upload font texture
    uint32_t fontResourceId = Efonts_defaultFont;
    uint32_t fontTextureId = gfx_upload_resource_ram(Efonts_defaultFont_texture);      
    
	static HID_Event lastEvt;
	uint32_t lastEvtTime = 0;


    while(picocom_running()) 
    {
		gfx_begin_frame();

        // fill bg
        gfx_fill_rect(0, 0, FRAME_W, FRAME_H, EColor16BPP_Black, 0xff);

        int lastY = 20;

        if( input_has_keyboard() )
        {
            gfx_draw_text(fontResourceId, fontTextureId, 10, lastY, "[has keyboard]", EColor16BPP_White); 

            lastY += 10;
        }


        if( input_has_gamepad() )
        {
            gfx_draw_text(fontResourceId, fontTextureId, 10, lastY, "[has gamepad]", EColor16BPP_White); 

            lastY += 10;
        }
                
        // Render last event
        if(lastEvtTime != 0 && picocom_time_us_32()-lastEvtTime < 3000000)
        {
            struct HID_Event* evt = &lastEvt;
            switch (evt->eventType) 
            {
                case EHIDEventType_Mouse:
                {
                    struct HID_MouseStateData* mouse = &evt->mouse;

                    char buff[256];
                    sprintf(buff, "mouse %d, %d, %x", mouse->deltaX, mouse->deltaY, mouse->buttons);
                    
                    gfx_draw_text(fontResourceId, fontTextureId, 10, lastY, buff, EColor16BPP_Green); 
                    lastY += 10;

                    break;
                }
                case EHIDEventType_Keyboard:
                {
                    struct HID_KeyboardStateData* kb = &evt->keyboard;
                    for(int i=0;i<HID_KeyboardStateData_keycode_Max;i++)
                    {
                        if(kb->keycode[i])
                        {                            
                            char buff[256];
                            sprintf(buff, "keypress '%c' (%d)\n", input_keycode_to_char(kb->keycode[i], kb->modifier), kb->keycode[i]);
                            
                            gfx_draw_text(fontResourceId, fontTextureId, 10, lastY, buff, EColor16BPP_Green); 
                            lastY += 10;
                        }
                    }

                    break;
                }
                case EHIDEventType_Gamepad:
                {
                    struct HID_GamepadStateData* gamepad = &evt->gamepad;
                    static uint8_t last_buttons = 0;
                    if(gamepad->buttons != last_buttons)
                    {
                        last_buttons = gamepad->buttons;

                        char buff[256];
                        sprintf(buff, "gamepad btn '%x'", gamepad->buttons);
                        
                        gfx_draw_text(fontResourceId, fontTextureId, 10, lastY, buff, EColor16BPP_Green); 
                        lastY += 10;                        
                    }
                    
                    static uint8_t last_dpad = 0;
                    if(gamepad->dpad != last_dpad)
                    {
                        last_dpad = gamepad->dpad;

                        char buff[256];
                        sprintf(buff, "gamepad dpad '%x'", gamepad->dpad);
                        
                        gfx_draw_text(fontResourceId, fontTextureId, 10, lastY, buff, EColor16BPP_Green); 
                        lastY += 10;                          
                    }                                                                                                                                                              
                    break;
                }        
            }                        
        }

        // Event processing for engine               
        while( input_event_count() > 0)   
        {
            struct HID_Event* evt = input_event_pop();
            if(!evt)
                break;

            lastEvtTime = picocom_time_us_32();
            lastEvt = *evt;                     
        }

        gfx_end_frame();
        picocom_update();        
    }
}
