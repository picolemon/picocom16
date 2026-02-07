#include <stdlib.h>
#include <math.h>
#include "demo.h"
#include "custom_gfx.h"
#include "lib/components/vdp1_core/vdp_client.h"
#include "lib/gpu/gpu_types.h"
#include "resources/models.inl"
#include "resources/textures.inl"


//
//
#define LERP(a,b,f) \
    (a + f * (b - a))


//
// Example custom arm shader code
enum ECustomGPUCmd
{       
    ECustomGPUCmd_UserBegin = EGPUCMD_UserCmdBegin,
    ECustomGPUCmd_DrawRadialGrad,
};


// 
// Radial grad
typedef struct DrawRadialGradState {
    uint32_t drawRadialGradShaderId;    
} DrawRadialGradState;


SHADER_FUN(DrawRadialGrad)(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, struct TileFrameBuffer_t* fb)
{         
    struct GPUMD_DrawRadialGrad* cmd = (struct GPUMD_DrawRadialGrad*)header;
    
    uint16_t* pixels = (uint16_t*)fb->pixelsData;

    int pixelBaseX = cmd->x;
    int pixelBaseY = cmd->y - fb->y;
    int pixelStartY = 0;   
    if(pixelBaseY < 0)
        pixelStartY = -pixelBaseY;

    // calc start pixel X ()
    int pixelStartX = 0;   
    if(pixelBaseX < 0)
        pixelStartX = -pixelBaseX;
    int pixelCntY = cmd->h;
    
    for(int y=pixelStartY;y<pixelCntY;y++)
    {
        // calc start pixel Y
        int localY = pixelBaseY + y;
        if(localY >= fb->h)
            break;
        int pixelRowStride = (localY*fb->w);
        int pixelCntX = cmd->w;

        for(int x=pixelStartX;x<pixelCntX;x++)
        {                    
            int localX = pixelBaseX + x;
            if(localX >= fb->w)
                break;

            int index = localX + pixelRowStride;

            // slow gradient op            
            float d;
            {    
                float x1 = cmd->pivotX;
                float y1 = cmd->pivotY;
                float x2 = localX;
                float y2 = localY + fb->y;
       
                d = ((SqrtCallback)gpu->jitMethods[EGpuJitMethod_sqrtf])( (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2) );
            }            
            float r = (1.0f / cmd->length) * d;
            if(r < 0)
                r = 0;
            else if(r > 1)
                r = 1;

            Col16_t c = {};
            Col16_t fromCol = {.value = cmd->toCol}; // flip so toCol is in center
            Col16_t toCol = {.value = cmd->fromCol};

            c.r = LERP(fromCol.r, toCol.r, r);
            c.g = LERP(fromCol.g, toCol.g, r);
            c.b = LERP(fromCol.b, toCol.b, r);

            if(index >= fb->w*fb->h)
                return;

            pixels[ index ] = c.value;;    
            if(fb->attr)
                fb->attr[index] = 0xff;                    
        }
    }      
}


void *demo_fill_radial_grad_init(struct DemoState* demo, struct DemoInfo* info)
{
    struct DrawRadialGradState* state = malloc(sizeof(*state));
    memset(state, 0, sizeof(DrawRadialGradState));

    // Alloc and upload shader
    uint32_t bufferId;
    uint32_t offset;
    
    if(!gfx_alloc_block(shader_get_size_DrawRadialGrad(), EGfxTargetVDP1, EGPUBufferArena_Ram0, &bufferId, &offset))
        picocom_panic( SDKErr_Fail, "alloc failed" );

    state->drawRadialGradShaderId = bufferId;
    shader_upload_DrawRadialGrad(ECustomGPUCmd_DrawRadialGrad, state->drawRadialGradShaderId, EGfxTargetVDP1, EGPUBufferArena_Ram0, offset);

    return state;
}


void demo_radial_grad_update(struct DemoState* demo, struct DemoInfo* info, void * instance)
{
    struct DrawRadialGradState* state = (struct DrawRadialGradState*)instance;
    struct VdpClientImpl_t* client = display_get_impl();
  
    gfx_begin_frame();        
    gfx_fill_rect(0, 0, FRAME_W, FRAME_H, EColor16BPP_Black, 0xff);

    // Custom cmd
    {
        struct GPUMD_DrawRadialGrad* fillCmd = (GPUMD_DrawRadialGrad*)vdp1_cmd_add_next(client, EGfxTargetVDP1, sizeof(GPUMD_DrawRadialGrad));
        if(!fillCmd)
            return;

        GPU_INIT_CMD(fillCmd, ECustomGPUCmd_DrawRadialGrad);
        fillCmd->fromCol = EColor16BPP_Black;
        fillCmd->toCol = EColor16BPP_Red;
        fillCmd->x = 10;
        fillCmd->y = 10;
        fillCmd->w = 110;
        fillCmd->h = 110; 
        fillCmd->pivotX = 60;
        fillCmd->pivotY = 60;
        fillCmd->length = 35;         
    }

    demo_render_overlay();    

    gfx_end_frame();
    
}


// 
// Trex demo
typedef struct TrexDrawMesh3DState {
    uint32_t trexMeshBufferId;   
    uint32_t trexTextureBufferId; 
    uint32_t cubeMeshBufferId;   
    uint32_t cubeTextureBufferId;     
    int32_t renderId;
} TrexDrawMesh3DState;


void *demo_trex_draw_mesh_init(struct DemoState* demo, struct DemoInfo* info)
{
    struct TrexDrawMesh3DState* state = malloc(sizeof(*state));
    memset(state, 0, sizeof(TrexDrawMesh3DState));

    gfx_mount_asset_pack(&asset_textures);   
    gfx_mount_asset_pack(&asset_models);  

    display_reset_gpu();
    gfx_init_renderer3d();

    state->trexMeshBufferId = gfx_upload_resource_flash(Emodels_trex0);
    state->trexTextureBufferId = gfx_upload_resource_flash(Etextures_trex0_texture);    

    state->cubeMeshBufferId = gfx_upload_resource_flash(Emodels_cube0);
    state->cubeTextureBufferId = 0;

    state->renderId = 0;

    return state;
}


void demo_trex_draw_mesh_update(struct DemoState* demo, struct DemoInfo* info, void * instance)
{
    static int loopnumber;
    struct TrexDrawMesh3DState* state = (struct TrexDrawMesh3DState*)instance;
    struct VdpClientImpl_t* client = display_get_impl();

    gfx_begin_frame();            
    
    const float end1 = 6000;
    const float end2 = 2000;
    const float end3 = 6000;
    const float end4 = 2000;

    int tot = (int)(end1 + end2 + end3 + end4);
    int m = picocom_time_ms_32();

    loopnumber = m / tot;
    float t = m % tot;
    const float roty = 360 * (t / 4000);  // rotate 1 turn every 4 seconds
    float tz, ty;
    const float dilat = 9;                // scale model
    const float nearz = 15; // how much we zoom in
    const float upy = 2; // how much we move up to focus on the head
    if (t < end1) {  // far away
        tz = -25;
        ty = 0;
    } else {
        t -= end1;
        if (t < end2) {  // zooming in
            t /= end2;
            tz = -25 + (25-nearz) * t;
            ty = -upy * t;
        } else {
            t -= end2;
            if (t < end3) {  // close up
                tz = -nearz;
                ty = -upy;
            } else {  // zooming out
                t -= end3;
                t /= end4;
                tz = -nearz - (25 -nearz) * t;
                ty = upy*(t-1);
            }
        }
    }
   
    switch (state->renderId)
    {
    case 0:
    {
        Matrix4 M;
        gfx_matrix_set_scale( &M, dilat, dilat, dilat);  // scale the model
        gfx_matrix_mult_rotate( &M, -roty, 0, 1, 0);     // rotate around y
        gfx_matrix_mult_translate( &M, 0, ty, tz);       // translate

        gfx_draw_begin_frame_tile3d( 45, ((float)FRAME_W) / FRAME_H, 1.0f, 100.0f, EColor16BPP_Red, true, true );            
        gfx_set_model_matrix3d( M.M );
        
        gfx_set_shader3d( GFX_SHADER_TEXTURE ); 
        gfx_draw_mesh3d( state->trexMeshBufferId, state->trexTextureBufferId, 1 );
        break;
    }
    case 1:
    {
        Matrix4 M;
        gfx_matrix_set_scale( &M, dilat*0.25f, dilat*0.25f, dilat*0.25f);  // scale the model
        gfx_matrix_mult_rotate( &M, -roty, 0, 1, 0);     // rotate around y
        gfx_matrix_mult_translate( &M, 0, ty, tz);       // translate

        gfx_draw_begin_frame_tile3d( 45, ((float)FRAME_W) / FRAME_H, 1.0f, 100.0f, EColor16BPP_Red, true, true );            
        gfx_set_model_matrix3d( M.M );

        gfx_set_shader3d( GFX_SHADER_GOURAUD ); 
        gfx_draw_mesh3d( state->cubeMeshBufferId, state->cubeTextureBufferId, 1 );
        break;
    }
    default:
        break;
    }

    gfx_end_frame();

    demo_render_overlay();    

    if(input_keyboard_is_keycode_up(KeyboardKeycode_Up))
    {
        state->renderId++;
        if( state->renderId >= 1)
            state->renderId = 0;
    }
    if(input_keyboard_is_keycode_up(KeyboardKeycode_Down))
    {
        state->renderId--;
        if( state->renderId < 0)
            state->renderId = 1;
    }
    
}



void register_complex_demos(DemoState* state)
{    
    state->demos[EDemoId_RadialGrad] = (struct DemoInfo){.name="RadialGrad", .init=demo_fill_radial_grad_init, .update=demo_radial_grad_update, .free=demo_default_free};    
    state->demos[EDemoId_TrexDrawMesh3D] = (struct DemoInfo){.name="TrexDrawMesh3D", .init=demo_trex_draw_mesh_init, .update=demo_trex_draw_mesh_update, .free=demo_default_free};    
}