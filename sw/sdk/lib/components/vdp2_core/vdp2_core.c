//#pragma GCC optimize ("O0")
#include "picocom/display/gfx.h"
#include "vdp2_core.h"
#include "lib/gpu/gpu_types.h"
#include "lib/gpu/gpu.h"
#include "picocom/devkit.h"
#include "picocom/display/gfx.h"
#include "platform/pico/vdp2/hw_vdp2_types.h"
#include <stdio.h>


// Fwd
void gpu_cmd_impl_CompositeTile(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
void gpu_cmd_impl_FillRectCol(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile);
void hid_update_sdl_state();


//
//
static void update_status_cmd(struct vdp2_t* vdp, struct VDP2CMD_GetStatus* status)
{    
    bus_tx_update_stats(vdp->vdp1_xlnk_tx, &status->bus_stats);
    bus_rx_update_stats(vdp->vdp1_vdbus_rx, &status->bus_stats);
}


static void vdp1_handler_realtime(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    struct vdp2_t* vdp = (struct vdp2_t*)bus->userData;

    switch(frame->cmd)
    {
    case EBusCmd_VDP2_GetStatus:
    {
        static struct VDP2CMD_GetStatus status  = {};        
        update_status_cmd(vdp, &status);
        
        BUS_INIT_CMD(status, EBusCmd_VDP2_GetStatus);
        bus_tx_rpc_set_return_irq(vdp->vdp1_xlnk_tx, frame, &status.header);
        break;
    }      
    default:
    {
        bus_rx_push_defer_cmd(bus, frame);            
        break;
    }      
    }
}


static void vdp2_handler_main(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    struct vdp2_t* vdp = (struct vdp2_t*)bus->userData;

    switch(frame->cmd)
    {  
    case EBusCmd_VDP1_DebugDump:
    {
        // gpu dump
        printf("VDP2 gpu state\n");
        gpu_debug_dump_state(vdp->gpuState, false );

        break;
    }        
    case EBusCmd_VDP2_TileFrameBuffer16bpp:      // VDP1 tile render
    {
        struct VDP2CMD_TileFrameBuffer* cmd = (struct VDP2CMD_TileFrameBuffer*)frame;
        
        // block waiting for tile job to release
        while(vdp->pendingTileCmd)
        {
            tight_loop_contents();
        }

        // copy job        
        memcpy(vdp->tileCmd, cmd, sizeof(struct VDP2CMD_TileFrameBuffer16bpp));

        // mark valid
        vdp->pendingTileCmd = vdp->tileCmd;  

        break;          
    }    
    case EBusCmd_VDP2_TileFrameBuffer8bpp:      // VDP1 tile render
    {
        struct VDP2CMD_TileFrameBuffer8bpp* cmd = (struct VDP2CMD_TileFrameBuffer8bpp*)frame;
        
        // block waiting for tile job to release
        while(vdp->pendingTileCmd)
        {
            tight_loop_contents();
        }

        // copy job ( shared type, size will be largest max struct size of each type )   
        memcpy(vdp->tileCmd, cmd, sizeof(struct VDP2CMD_TileFrameBuffer8bpp));

        // mark valid
        vdp->pendingTileCmd = vdp->tileCmd;  

        break;          
    }         
    case EBusCmd_VDP1_ForwardVDP2CmdData:  // VDP1 forwarding commands from app
    {
        struct VDP1CMD_DrawCmdData* cmd = (struct VDP1CMD_DrawCmdData*)frame;

        struct GpuCommandList_t cmdList = {0};
        cmdList.headerSz = 0;
        cmdList.allocSz = cmd->header.sz;
        cmdList.offset = 0;
        cmdList.cmdCount = cmd->cmdDataCount; 
        cmdList.cmdData = cmd->cmdData;

        // null frame
        TileFrameBuffer_t dstTileBuffer = {
            .pixelsData = 0,
            .attr = 0,  
            .y = 0,
            .w = 0,
            .h = 0,
            .tileId = 0, 
        };
               
        // exec headless gpu cmds        
        struct GpuInstance_t* gpuInstance = &vdp->gpuInstances[0];
        gpu_clear_error_stats(vdp->gpuState, gpuInstance);
        gpu_begin_frame(vdp->gpuState, gpuInstance, 0);       
        gpu_run_tile(vdp->gpuState, gpuInstance, &cmdList, &dstTileBuffer);

        break;
    }     
    }
}


int vdp2_init(struct vdp2_t* vdp, struct vdp2InitOptions_t* options)
{
    if(vdp->gpuState)
        return SDKErr_Fail;

    // init display driver
    if(options->initDisplayDriver)
        display_driver_init();

    // create bus uplinks
    vdp->vdp1_xlnk_tx = options->vdp1_xlnk_tx;
    vdp->vdp1_xlnk_tx->tx_ack_timeout = 0; // no timeout
    vdp->vdp1_xlnk_tx->userData = vdp;

    vdp->vdp1_vdbus_rx = options->vdp1_vdbus_rx;
    vdp->vdp1_vdbus_rx->userData = vdp;

    bus_rx_set_callback(vdp->vdp1_vdbus_rx, vdp1_handler_realtime, vdp2_handler_main);

    // alloc gpu
    vdp->gpuState = gpu_init(&options->gpuOptions);
    if(!vdp->gpuState)
        return SDKErr_Fail;
    vdp->gpuState->debugName = "vdp2";
    gpu_init_instance(vdp->gpuState, &vdp->gpuInstances[0], 0);

    // alloc copies
    vdp->tileCmd = picocom_malloc(sizeof(VDP2CMD_TileFrameBuffer16bpp));
    if(!vdp->tileCmd)
        return SDKErr_Fail;

#ifdef ENABLE_VDP2_GPU    
    // Alloc copy of max cmd    
    vdp->drawCmdMaxSz = vdp->vdp1_vdbus_rx->rx_buffer_size;
    vdp->drawCmd = picocom_malloc(vdp->drawCmdMaxSz);
    if(!vdp->drawCmd)
        return SDKErr_Fail;
#endif

    return SDKErr_OK;
}


int vdp2_deinit(struct vdp2_t* vdp)
{
    if(vdp->gpuState)
        gpu_deinit(vdp->gpuState);

    // de-init display
    display_driver_deinit();

    // clear state
    vdp->gpuState = 0;
    vdp->vdp1_xlnk_tx = 0;
    vdp->vdp1_vdbus_rx = 0;

    return SDKErr_OK;
}


// CREDIT: https://stackoverflow.com/questions/18937701/combining-two-16-bits-rgb-colors-with-alpha-blending
static uint16_t ALPHA_BLIT16_565(uint32_t fg, uint32_t bg, uint8_t alpha) {
    // Alpha converted from [0..255] to [0..31]
    uint32_t ALPHA = alpha >> 3;     
    fg = (fg | fg << 16) & 0x07e0f81f;
    bg = (bg | bg << 16) & 0x07e0f81f;
    bg += (fg - bg) * ALPHA >> 5;
    bg &= 0x07e0f81f;
    return (uint16_t)(bg | bg >> 16);
}


void vdp2_write_tile16bpp(struct vdp2_t* vdp, struct VDP2CMD_TileFrameBuffer16bpp* cmd)
{    
    struct GpuCommandList_t cmdList = {0};
    cmdList.headerSz = 0;
    cmdList.allocSz = cmd->header.sz;
    cmdList.offset = 0;
    cmdList.cmdCount = cmd->vdp2CmdDataCount; 
    cmdList.cmdData = cmd->vdp2CmdData;

    //gpu_dump_cmds(0, &cmdList);

    struct GpuInstance_t* gpuInstance = &vdp->gpuInstances[0];
    uint16_t* main_buffer = get_display_buffer();
    if(!main_buffer)
    {
        return;
    }

#ifdef VDP_VALIDATE_CMDS
    GpuValidationOutput_t validator;
    if(!gpu_validate_cmds_list(vdp->gpuState, &cmdList, &validator))
    {
        gpu_dump_cmds(vdp->gpuState, &cmdList);
        gpu_print_validator_error( &validator );
        
        printf("!cmds not valid\n");
    }
#endif

        uint32_t rowIndex = cmd->tileId*FRAME_TILE_SZ_Y;        
        uint16_t* dstRow = main_buffer + (FRAME_W*rowIndex);
        
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"          
        TileFrameBuffer_t dstTileBuffer = {
            .pixelsData = (uint8_t*)dstRow,               // Get ptr in back buffer
            .attr = 0,                      // no dest alpha
            .y = rowIndex,
            .w = FRAME_W,
            .h = VDP_TILEFRAME_BUFFER_Y_SIZE,
            .tileId = cmd->tileId, 
            .colorDepth = EColorDepth_BGR565
        };
                
        TileFrameBuffer_t srcTileBuffer = {
            .pixelsData = (uint8_t*)cmd->pixels,               // Get src pixel
            .attr=cmd->attr,                    // Get src attr eg. alpha
            .y = rowIndex,
            .w = FRAME_W,
            .h = VDP_TILEFRAME_BUFFER_Y_SIZE,
            .tileId = cmd->tileId, 
            .colorDepth = EColorDepth_BGR565
        };
#pragma GCC diagnostic pop   

    // get create tile buffer for gpu
    if(cmdList.cmdCount > 0)
    {        
        // exec gpu cmds
        gpu_clear_error_stats(vdp->gpuState, gpuInstance);
        gpu_begin_frame(vdp->gpuState, gpuInstance, 0);                                  // reset gpu state every tile, not 100% correct but blend states need to be reset
        gpu_bind_fbo(vdp->gpuState, gpuInstance, &srcTileBuffer);

        gpu_run_tile(vdp->gpuState, gpuInstance, &cmdList, &dstTileBuffer);              // Render command into tile
    } 
    else // default copy
    {
        // bind fb to buffer        
        gpu_bind_fbo(vdp->gpuState, gpuInstance, &srcTileBuffer);

        // clear pass zero
        if(cmd->passId == 0)
        {
            struct GPUCMD_FillRectCol fillCmd = {0};
            fillCmd.col = 0;
            fillCmd.a = 0;
            fillCmd.x = 0;
            fillCmd.y = 0;
            fillCmd.w = FRAME_W;
            fillCmd.h = FRAME_H;
            gpu_cmd_impl_FillRectCol(vdp->gpuState, gpuInstance, &fillCmd.header, &dstTileBuffer); 
        }

        // run default comp
        {
            struct GPUCMD_CompositeTile compCmd = {0};
            compCmd.palBufferId = 0;
            compCmd.blendMode = cmd->defaultBlendMode;

            gpu_cmd_impl_CompositeTile(vdp->gpuState, gpuInstance, &compCmd.header, &dstTileBuffer);
        }
    }       
}


void vdp2_write_tile8bpp(struct vdp2_t* vdp, struct VDP2CMD_TileFrameBuffer8bpp* cmd)
{    
    struct GpuCommandList_t cmdList = {0};
    cmdList.headerSz = 0;
    cmdList.allocSz = cmd->header.sz;
    cmdList.offset = 0;
    cmdList.cmdCount = cmd->vdp2CmdDataCount; 
    cmdList.cmdData = cmd->vdp2CmdData;
    
    //gpu_dump_cmds(0, &cmdList);

    struct GpuInstance_t* gpuInstance = &vdp->gpuInstances[0];
    uint16_t* main_buffer = get_display_buffer();
    if(!main_buffer)
    {
        return;
    }

#ifdef VDP_VALIDATE_CMDS
    GpuValidationOutput_t validator;
    if(!gpu_validate_cmds_list(vdp->gpuState, &cmdList, &validator))
    {
        gpu_dump_cmds(vdp->gpuState, &cmdList);
        gpu_print_validator_error( &validator );
        
        printf("!cmds not valid\n");
    }
#endif

        uint32_t rowIndex = cmd->tileId*FRAME_TILE_SZ_Y;        
        uint16_t* dstRow = main_buffer + (FRAME_W*rowIndex);
        
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"          
        TileFrameBuffer_t dstTileBuffer = {
            .pixelsData = (uint8_t*)dstRow,               // Get ptr in back buffer
            .attr = 0,                      // no dest alpha
            .y = rowIndex,
            .w = FRAME_W,
            .h = VDP_TILEFRAME_BUFFER_Y_SIZE,
            .tileId = cmd->tileId, 
            .colorDepth = EColorDepth_BGR565
        };
                
        TileFrameBuffer_t srcTileBuffer = {
            .pixelsData = (uint8_t*)cmd->pixels,               // Get src pixel
            .attr=0,                    // Get src attr eg. alpha
            .y = rowIndex,
            .w = FRAME_W,
            .h = VDP_TILEFRAME_BUFFER_Y_SIZE,
            .tileId = cmd->tileId, 
            .colorDepth = EColorDepth_8BPP
        };
#pragma GCC diagnostic pop   

    // get create tile buffer for gpu
    if(cmdList.cmdCount > 0)
    {        
        // exec gpu cmds
        gpu_clear_error_stats(vdp->gpuState, gpuInstance);
        gpu_begin_frame(vdp->gpuState, gpuInstance, 0);                                  // reset gpu state every tile, not 100% correct but blend states need to be reset
        gpu_bind_fbo(vdp->gpuState, gpuInstance, &srcTileBuffer);

        gpu_run_tile(vdp->gpuState, gpuInstance, &cmdList, &dstTileBuffer);              // Render command into tile
    } 
    else // default copy
    {
        // bind fb to buffer        
        gpu_bind_fbo(vdp->gpuState, gpuInstance, &srcTileBuffer);

        // clear pass zero
        if(cmd->passId == 0)
        {
            struct GPUCMD_FillRectCol fillCmd = {0};
            fillCmd.col = 0;
            fillCmd.a = 0;
            fillCmd.x = 0;
            fillCmd.y = 0;
            fillCmd.w = FRAME_W;
            fillCmd.h = FRAME_H;
            gpu_cmd_impl_FillRectCol(vdp->gpuState, gpuInstance, &fillCmd.header, &dstTileBuffer); 
        }

        // run default comp
        {
            struct GPUCMD_CompositeTile compCmd = {0};
            compCmd.blendMode = cmd->defaultBlendMode;
            compCmd.palBufferId = cmd->globalVdp2PalBufferId;

            gpu_cmd_impl_CompositeTile(vdp->gpuState, gpuInstance, &compCmd.header, &dstTileBuffer);
        }
    }       
}


int vdp2_main(struct vdp2_t* vdp, struct vdp2MainLoopOptions_t* options)
{
    // update loop (if options set)
    int counter = 0;
    while (1)
    {           
        vdp2_update( vdp, 0 );

#ifdef PICOCOM_SDL        
        picocom_sleep_us(1);
#endif      
    }
    return 0;
}

bool hasBusCmd = false;

int vdp2_update(struct vdp2_t* vdp, struct vdp2MainLoopOptions_t* options)
{
    bus_rx_update(vdp->vdp1_vdbus_rx);
    bus_tx_update(vdp->vdp1_xlnk_tx);    

    if(vdp->pendingTileCmd)
    {        
        switch (vdp->pendingTileCmd->cmd)
        {
        case EBusCmd_VDP2_TileFrameBuffer16bpp:            
        {
            struct VDP2CMD_TileFrameBuffer16bpp* tileCmd = (struct VDP2CMD_TileFrameBuffer16bpp*)vdp->pendingTileCmd;

            uint32_t cmdFlags = tileCmd->cmdFlags;
            uint32_t passId = tileCmd->passId;

            // comp tile into frame
            vdp2_write_tile16bpp(vdp, tileCmd);

            // mark free
            vdp->pendingTileCmd = 0;

            // handle frame flipping
            // NOTE: could do this async on core2, this will block slowing down core1
            if(cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay)
            {
                flip_display_blocking(); 
                vdp->flipCount++;
            }
            if(cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_CopyFB)
            {
                display_buffer_copy_front(); 
            } 
            break;
        }
        case EBusCmd_VDP2_TileFrameBuffer8bpp:            
        {
            struct VDP2CMD_TileFrameBuffer8bpp* tileCmd = (struct VDP2CMD_TileFrameBuffer8bpp*)vdp->pendingTileCmd;

            uint32_t cmdFlags = tileCmd->cmdFlags;
            uint32_t passId = tileCmd->passId;
            
            // comp tile into frame
            vdp2_write_tile8bpp(vdp, tileCmd);

            // mark free
            vdp->pendingTileCmd = 0;

            // handle frame flipping
            // NOTE: could do this async on core2, this will block slowing down core1
            if(cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay)
            {
                flip_display_blocking(); 
                vdp->flipCount++;
            }
            if(cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_CopyFB)
            {
                display_buffer_copy_front(); 
            } 
            break;
        }            
        default:
            break;
        }
    }
        
    #ifdef PICOCOM_SDL
        // sdl sim runs on vdp2 loop, inputs needs to be processed there
        hid_update_sdl_state();
    #endif

    // Display timeout
    if(vdp->flipCount == 0 && picocom_time_ms_32() - vdp->startupTime > 1000)
    {
        vdp_render_error( vdp, 1, "VDP2 IDLE");
    }

    return 0;
}


void vdp_render_error(struct vdp2_t* vdp, uint32_t errorCode, const char* msg)
{
    struct GpuInstance_t* gpuInstance = &vdp->gpuInstances[0];
    uint16_t* main_buffer = get_display_buffer();
    
    for(int j=0;j<FRAME_H;j++)
    {
        for(int i=0;i<FRAME_W;i++)            
        {
            if(i > 213)
                main_buffer[i + (j*FRAME_W)] = EColor16BPP_Blue;
            else if(i > 106)
                main_buffer[i + (j*FRAME_W)] = EColor16BPP_Green;
            else
                main_buffer[i + (j*FRAME_W)] = EColor16BPP_Red;
        }
    }

    flip_display_blocking(); 
}
