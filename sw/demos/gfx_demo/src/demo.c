#pragma GCC optimize ("O0")
#include "demo.h"
#include "resources/fonts.inl"
#include "picocom/display/display.h"
#include <stdlib.h>
#include <stdio.h>


// Globals
struct DemoState g_DemoState = {};
uint32_t g_NextDemoId = -1;

// Fwd
void register_simple_demos(DemoState* state);
void register_complex_demos(DemoState* state);
void register_mini3d_demo(DemoState* state);
void demo_set_index_impl();


//
//
void demo_init()
{
    memset(&g_DemoState, 0, sizeof(g_DemoState));    
    register_simple_demos(&g_DemoState);
    register_complex_demos(&g_DemoState);
    g_DemoState.demoId = -1;
    g_DemoState.drawFps = true;

    display_reset_gpu();

    // set init demo
    demo_set_index(DEMO_INIT_DEMO_ID);    
}


void demo_handle_inputs()
{
    int demoId = g_DemoState.demoId;
    if(input_keyboard_is_keycode_up(KeyboardKeycode_Right))
    {
        demoId++;
        if( demoId >= EDemoId_Max)
            demoId = 0;

        demo_set_index(demoId);
    }
    if(input_keyboard_is_keycode_up(KeyboardKeycode_Left))
    {
        demoId--;
        if( demoId < 0)
            demoId = EDemoId_Max - 1;

        demo_set_index(demoId);
    }

    if(input_gamepad_button_is_up(GamepadButton_Select))
    {
        demoId++;
        if( demoId >= EDemoId_Max)
            demoId = 0;

        demo_set_index(demoId);
    }
}


void demo_render_overlay()
{
    struct DemoInfo* info = &g_DemoState.demos[g_DemoState.demoId];

    // draw text
    if((picocom_time_us_32() - g_DemoState.showDemoNameTime) < 1000000*5 &&  info->name)
    {
        gfx_draw_text(g_DemoState.fontResourceId, g_DemoState.fontTextureId, 1, 1, info->name, EColor16BPP_Black); 
        gfx_draw_text(g_DemoState.fontResourceId, g_DemoState.fontTextureId, 2, 2, info->name, EColor16BPP_White); 
    }

    // draw fps
    if(g_DemoState.drawFps)
    {
      struct DisplayStats_t* stats =  display_stats();
      char buff[32];
      sprintf(buff, "%0.2f", stats->avgFps );
      gfx_draw_text(g_DemoState.fontResourceId, g_DemoState.fontTextureId, FRAME_W-32, 1, buff, EColor16BPP_White); 
    }    

    input_update();
    demo_handle_inputs();
}


void demo_update()
{
    if(g_DemoState.demoId != -1)
    {
        struct DemoInfo* info = &g_DemoState.demos[g_DemoState.demoId];
        if(info->update)
        {
            if(info->init && !g_DemoState.demoInstance)
                picocom_panic(SDKErr_Fail,"!g_DemoState.demoInstance");
            info->update(&g_DemoState, info, g_DemoState.demoInstance);
        }
        else
        {  
            gfx_begin_frame();
            demo_render_overlay();    
            gfx_end_frame();
        }
    }

    demo_set_index_impl();
}


void demo_set_index(uint32_t i)
{
    printf("demo_set_index %d\n", i);
    g_NextDemoId = i;
}


void demo_set_index_impl()
{
    if(g_NextDemoId == -1)
        return;

    uint32_t i = g_NextDemoId;
    g_NextDemoId = -1;

    printf("demo_set_index_impl %d\n", i);
    if( i >= EDemoId_Max)
        i = EDemoId_Max - 1;

    if(g_DemoState.demoInstance)
    {        
        struct DemoInfo* info = &g_DemoState.demos[g_DemoState.demoId];
        // free
        g_DemoState.demos[g_DemoState.demoId].free(&g_DemoState, info, g_DemoState.demoInstance);

        g_DemoState.demoInstance = 0;
    }

    // set next
    g_DemoState.demoId = i;
    g_DemoState.showDemoNameTime = picocom_time_us_32();

    gfx_deinit();

    // init gfx    
    gfx_init();    
    display_reset_gpu();

    gfx_mount_asset_pack(&asset_fonts);   

    // upload font texture
    g_DemoState.fontResourceId = Efonts_defaultFont;
    g_DemoState.fontTextureId = gfx_upload_resource_ram(Efonts_defaultFont_texture);      
    
    // reset any shared states
    g_DemoState.state0 = -1;

    // init
    {
        struct DemoInfo* info = &g_DemoState.demos[g_DemoState.demoId];

        if(g_DemoState.demos[g_DemoState.demoId].init)
        {
            if(!info->free)
                picocom_panic(SDKErr_Fail, "Demo does not provide free handler");

            g_DemoState.demoInstance = g_DemoState.demos[g_DemoState.demoId].init(&g_DemoState, info);            
        }
    }
}


void demo_default_free(struct DemoState* demo, struct DemoInfo* info, void * instance)
{
    if(instance)
        free(instance);
}
