#include "demo.h"
#include "picocom/devkit.h"
#include "picocom/input/input.h"
#include "picocom/display/display.h"
#include "gpu/gpu.h"
#include <stdlib.h>
#include "resources/textures.inl"
#include <stdio.h>


//
//
void demo_draw_line_update(struct DemoState* demo, struct DemoInfo* info, void * instance)
{  
    gfx_begin_frame();
    
    static int16_t x0 = 0;
    static int16_t y0 = 0;
    static int16_t x1 = FRAME_W;
    static int16_t y1 = FRAME_H;    

    static int16_t x0_dir = 1;
    static int16_t y0_dir = 1;
    static int16_t x1_dir = 1;
    static int16_t y1_dir = 1;        

    gfx_fill_rect(0, 0, FRAME_W, FRAME_H, EColor16BPP_Blue, 0xff);
    gfx_draw_line( x0,  y0, x1, y1, EColor16BPP_Red );
    
    demo_render_overlay();    

    x0 += x0_dir;
    y0 += y0_dir;
    x1 += x1_dir;
    y1 += y1_dir;

    if( x0 > FRAME_W || x0 < 0 )
        x0_dir = -x0_dir;

    if( x1 > FRAME_W || x1 < 0 )
        x1_dir = -x1_dir;

    if( y0 > FRAME_H || y0 < 0 )
        y0_dir = -y0_dir;

    if( y1 > FRAME_H || y1 < 0 )
        y1_dir = -y1_dir;

    gfx_end_frame();
}


//
// solid fill
void demo_fill_rect_update(struct DemoState* demo, struct DemoInfo* info, void * instance)
{  
    gfx_begin_frame();
    
    // fill bg    
    gfx_fill_rect(0, 0, FRAME_W, FRAME_H, EColor16BPP_Black, 0xff);    
    gfx_fill_rect(0, 0, FRAME_W, FRAME_H, EColor16BPP_Black, 0xff);
    gfx_fill_rect(0, 0, 32, 64, 0b0000000000011111, 0xff);
    gfx_fill_rect(32, 0, 32, 64, 0b0000011111100000, 0xff);
    gfx_fill_rect(64, 0, 32, 64, 0b1111100000000000, 0xff);
    
    demo_render_overlay();    

    gfx_end_frame();
}


//
// Blit
typedef struct BlitFillState {
    uint32_t blobTexId;
    uint16_t x, y;
    int is_controlled;
} BlitFillState;


void *demo_blit_init(struct DemoState* demo, struct DemoInfo* info)
{
    struct BlitFillState* state = malloc(sizeof(BlitFillState));
    memset(state, 0, sizeof(*state));

    gfx_mount_asset_pack(&asset_textures);   
    //gfx_mount_asset_pack(&asset_models);   

    state->blobTexId = gfx_upload_resource_ram(Etextures_blob0_texture);

    return state;
}


void demo_blit_update(struct DemoState* demo, struct DemoInfo* info, void * instance)
{
    struct BlitFillState* state = (struct BlitFillState*)instance;
    
    gfx_begin_frame();        
    gfx_fill_rect(0, 0, FRAME_W, FRAME_H, EColor16BPP_Blue, 0xff);

    const struct GfxTextureInfo* tex = (struct GfxTextureInfo*)gfx_get_resource_info_of_type(Etextures_blob0_texture, EGfxAssetType_Texture);
    if(!tex)
        picocom_panic(SDKErr_Fail, "Failed to get texture");
    gfx_draw_texture(state->blobTexId, state->x, state->y, 0, 0, tex->w, tex->h, 0, 0); // Draw texture to screen        
    
    demo_render_overlay();    

    gfx_end_frame();

    if( state->is_controlled )
    {
        if(input_gamepad_dpad() & GamepadDpad_Left)
            state->x--;
        if(input_gamepad_dpad() & GamepadDpad_Right)
            state->x++;
        if(input_gamepad_dpad() & GamepadDpad_Up)
            state->y--;
        if(input_gamepad_dpad() & GamepadDpad_Down)
            state->y++;
    }
    else 
    {
        state->x++;
        state->y++;            
    }

    if(state->x > FRAME_W)
        state->x= 0;
    if( state->x < 0)
        state->x= FRAME_W;

    if(state->y > FRAME_H)
        state->y = 0;            
    if( state->y < 0)
        state->y= FRAME_H;

    if(input_gamepad_button_is_down(GamepadButton_Start))
    {
        state->is_controlled = 1;
    }
}


//
// Draw text
void demo_fill_draw_text(struct DemoState* demo, struct DemoInfo* info, void * instance)
{    
    gfx_begin_frame();

    gfx_fill_rect(0, 0, FRAME_W, FRAME_H, EColor16BPP_Blue, 0xff);

    gfx_draw_text(demo->fontResourceId, demo->fontTextureId, 10, 20, "Sample text draw!", EColor16BPP_White); 
    
    demo_render_overlay();    

    gfx_end_frame();
}


//
//
void register_simple_demos(DemoState* state)
{
    state->demos[EDemoId_DrawLine] = (struct DemoInfo){.name="DrawLine", .update=demo_draw_line_update};    
    state->demos[EDemoId_FillRect] = (struct DemoInfo){.name="FillRect", .update=demo_fill_rect_update};    
    state->demos[EDemoId_Blit] = (struct DemoInfo){.name="Blit", .init=demo_blit_init, .update=demo_blit_update, .free=demo_default_free};    
    state->demos[EDemoId_DrawText] = (struct DemoInfo){.name="DrawText", .update=demo_fill_draw_text};                
}