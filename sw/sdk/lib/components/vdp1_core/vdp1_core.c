#include "picocom/devkit.h"
#include "vdp1_core.h"
#include "platform/pico/vdp1/hw_vdp1_types.h"
#include "platform/pico/vdp2/hw_vdp2_types.h"
#include "platform/pico/hw/picocom_hw.h"
#include "picocom/utils/profiler.h"
#include <stdio.h>
#include <assert.h>


//
//
static void update_status_cmd(struct vdp1_t* vdp, struct VDP1CMD_GetStatus* status)
{
    status->vdpState = vdp->state;
    status->isOnline = true;
}


static void app_handler_realtime(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    struct vdp1_t* vdp = (struct vdp1_t*)bus->userData;

    switch(frame->cmd)
    {  
    default:
    {
        bus_rx_push_defer_cmd(bus, frame);            
        break;
    }    
    }
}


static void vdp1_send_tile_job(struct vdp1_t* vdp, uint32_t coreId, struct tileListJob_t* job)
{
    switch (job->cmdIn->colorDepth)
    {
    case EColorDepth_BGR565:
    {
        const struct VDP1CMD_DrawCmdData* cmd = job->cmdIn;
        struct VDP2CMD_TileFrameBuffer16bpp* tileCmdOut = (struct VDP2CMD_TileFrameBuffer16bpp*)job->tileCmdOut;    

        if(cmd->cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_WriteVDP2Tile ) 
        {        
            uint32_t tileBusCopyStartTime = picocom_time_us_32();

            BEGIN_PROFILE()
            mutex_enter_blocking(&vdp->sendLock);

            // wait bus                        
            bus_tx_wait(vdp->vdp2_vdbus_tx);  

            //printf("[vdp1] upload tile passId:%d, tileId: %d\n", job->cmdIn->passId, job->tileFrameBuffer.tileId);
            
            // write jobA to vdp2
            //LOG_BUS_CMD("[vdp1] upload tile cmdSeqNum:%d, tileId: %d\n", job->cmdSeqNum, job->tileFrameBuffer.tileId);
            bus_tx_write_async(vdp->vdp2_vdbus_tx, (uint8_t*)&tileCmdOut->header, sizeof(*tileCmdOut));

            // wait bus                        
            bus_tx_wait(vdp->vdp2_vdbus_tx);

            mutex_exit(&vdp->sendLock);

            // profile
            job->tileBusCopyTookTime = picocom_time_us_32() - tileBusCopyStartTime;    

            //END_PROFILE_BLOCK_US();
            //printf("\tvdp1_send_tile_job[%d] took: %d us (%d ms)\n", coreId, t1-t0, (t1-t0)/1000); 
        }         
        break;
    }
    case EColorDepth_8BPP:
    {
        const struct VDP1CMD_DrawCmdData* cmd = job->cmdIn;
        struct VDP2CMD_TileFrameBuffer8bpp* tileCmdOut = (struct VDP2CMD_TileFrameBuffer8bpp*)job->tileCmdOut;    

        if(cmd->cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_WriteVDP2Tile ) 
        {        
            uint32_t tileBusCopyStartTime = picocom_time_us_32();

            BEGIN_PROFILE()
            mutex_enter_blocking(&vdp->sendLock);

            // wait bus                        
            bus_tx_wait(vdp->vdp2_vdbus_tx);  

            //printf("[vdp1] upload tile passId:%d, tileId: %d\n", job->cmdIn->passId, job->tileFrameBuffer.tileId);
            
            // write jobA to vdp2
            //LOG_BUS_CMD("[vdp1] upload tile cmdSeqNum:%d, tileId: %d\n", job->cmdSeqNum, job->tileFrameBuffer.tileId);
            bus_tx_write_async(vdp->vdp2_vdbus_tx, (uint8_t*)&tileCmdOut->header, sizeof(*tileCmdOut));

            // wait bus                        
            bus_tx_wait(vdp->vdp2_vdbus_tx);

            mutex_exit(&vdp->sendLock);

            // profile
            job->tileBusCopyTookTime = picocom_time_us_32() - tileBusCopyStartTime;    

            //END_PROFILE_BLOCK_US();
            //printf("\tvdp1_send_tile_job[%d] took: %d us (%d ms)\n", coreId, t1-t0, (t1-t0)/1000); 
        }         
        break;
    }    
    default:
        break;
    }      
}


static void vdp1_render_tile_job(struct vdp1_t* vdp, uint32_t coreId, struct tileListJob_t* job)
{             
    uint32_t tileId = job->tileId;
    uint32_t tileCmdFlags = 0;
    const struct VDP1CMD_DrawCmdData* cmd = job->cmdIn;

    switch (job->cmdIn->colorDepth)
    {
    case EColorDepth_BGR565:
    {
        struct VDP2CMD_TileFrameBuffer16bpp* tileCmdOut = (struct VDP2CMD_TileFrameBuffer16bpp*)job->tileCmdOut;

        struct GpuInstance_t gpuInstance;
        gpu_init_instance(vdp->gpuState, &gpuInstance, coreId);

        BUS_INIT_CMD_PTR(tileCmdOut, EBusCmd_VDP2_TileFrameBuffer16bpp);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"        
        job->tileFrameBuffer.pixelsData = (uint8_t*)(tileCmdOut->pixels + ( job->subTileId ?  (FRAME_W*(FRAME_TILE_SZ_Y / 2)) : 0 ));
        job->tileFrameBuffer.attr = tileCmdOut->attr +  ( job->subTileId ?  (FRAME_W*(FRAME_TILE_SZ_Y / 2)) : 0 );
        job->tileFrameBuffer.w = FRAME_W;
        job->tileFrameBuffer.h = FRAME_TILE_SZ_Y / 2;
        job->tileFrameBuffer.tileId = tileId;
        job->tileFrameBuffer.y = (tileId * FRAME_TILE_SZ_Y) + (job->subTileId * (FRAME_TILE_SZ_Y / 2));
        job->tileFrameBuffer.colorDepth = EColorDepth_BGR565;
#pragma GCC diagnostic pop  

        struct GpuCommandList_t cmdList = {0};
        cmdList.headerSz = 0;
        cmdList.allocSz = cmd->header.sz;
        cmdList.offset = 0;
        cmdList.cmdCount = cmd->cmdDataCount; 
        cmdList.cmdData = (uint8_t*)cmd->cmdData; // no const

        // handle flip on last tile ( dont allow flipping between tiles)
        if(tileId == FRAME_TILE_CNT_Y-1)
        {
            if(cmd->cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay)
                tileCmdFlags |= EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay;
            if(cmd->cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_CopyFB)
                tileCmdFlags |= EVDP1CMD_DrawCmdData_completeFlags_CopyFB;                                
        }

        // Tile filter
        if( (cmd->tileMask & (1 << tileId)) != 0 )
        {
            BEGIN_PROFILE();        

            // copy cmds
            tileCmdOut->vdp2CmdDataCount = cmd->vdp2CmdDataCount;
            tileCmdOut->vdp2CmdDataSz = cmd->vdp2CmdDataSz;
            memcpy(tileCmdOut->vdp2CmdData, cmd->vdp2CmdData, sizeof(cmd->vdp2CmdData));

            uint32_t tileRenderStartTime = picocom_time_us_32();

            // run job in core1 ( in parallel with core 2 )
            memset( job->tileFrameBuffer.pixelsData, 0, sizeof(tileCmdOut->pixels) / 2);    
            memset( job->tileFrameBuffer.attr , 0, sizeof(tileCmdOut->attr) / 2);

            // render gpu cmds                
            gpu_clear_error_stats(vdp->gpuState, &gpuInstance);
            gpu_begin_frame(vdp->gpuState, &gpuInstance, cmd->cmdSeqNum);
            gpu_run_tile(vdp->gpuState, &gpuInstance, (GpuCommandList_t*)&cmdList, &job->tileFrameBuffer);            // Render command into tile
            gpu_end_frame(vdp->gpuState, &gpuInstance);

            //picocom_sleep_ms(10);
            //END_PROFILE();
            //printf("\tvdp1_render_tile_job[%d] took: %d us (%d ms)\n", coreId, t1-t0, (t1-t0)/1000); 

            // build vdp2 cmd
            tileCmdOut->tileId = job->tileFrameBuffer.tileId;
            tileCmdOut->cmdSeqNum = cmd->cmdSeqNum;
            tileCmdOut->cmdFlags = tileCmdFlags;
            tileCmdOut->passId = cmd->passId;
            tileCmdOut->defaultBlendMode = cmd->defaultBlendMode;

            // add stats
            job->cmdErrors = gpuInstance.frameStats.cmdErrors;
            job->tileRenderTookTime = picocom_time_us_32() - tileRenderStartTime;                   
        } 
        break;
    }       
    case EColorDepth_8BPP:
    {
        struct VDP2CMD_TileFrameBuffer8bpp* tileCmdOut = (struct VDP2CMD_TileFrameBuffer8bpp*)job->tileCmdOut;

        struct GpuInstance_t gpuInstance;
        gpu_init_instance(vdp->gpuState, &gpuInstance, coreId);

        BUS_INIT_CMD_PTR(tileCmdOut, EBusCmd_VDP2_TileFrameBuffer8bpp);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"        
        job->tileFrameBuffer.pixelsData = tileCmdOut->pixels + ( job->subTileId ?  (FRAME_W*(FRAME_TILE_SZ_Y / 2)) : 0 );        
        job->tileFrameBuffer.w = FRAME_W;
        job->tileFrameBuffer.h = FRAME_TILE_SZ_Y / 2;
        job->tileFrameBuffer.tileId = tileId;
        job->tileFrameBuffer.y = (tileId * FRAME_TILE_SZ_Y) + (job->subTileId * (FRAME_TILE_SZ_Y / 2));
        job->tileFrameBuffer.colorDepth = EColorDepth_8BPP;
#pragma GCC diagnostic pop  

        struct GpuCommandList_t cmdList = {0};
        cmdList.headerSz = 0;
        cmdList.allocSz = cmd->header.sz;
        cmdList.offset = 0;
        cmdList.cmdCount = cmd->cmdDataCount; 
        cmdList.cmdData = (uint8_t*)cmd->cmdData; // no const

        // handle flip on last tile ( dont allow flipping between tiles)
        if(tileId == FRAME_TILE_CNT_Y-1)
        {
            if(cmd->cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay)
                tileCmdFlags |= EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay;
            if(cmd->cmdFlags & EVDP1CMD_DrawCmdData_completeFlags_CopyFB)
                tileCmdFlags |= EVDP1CMD_DrawCmdData_completeFlags_CopyFB;                                
        }

        // Tile filter
        if( (cmd->tileMask & (1 << tileId)) != 0 )
        {
            //BEGIN_PROFILE();        

            // copy cmds
            tileCmdOut->vdp2CmdDataCount = cmd->vdp2CmdDataCount;
            tileCmdOut->vdp2CmdDataSz = cmd->vdp2CmdDataSz;
            memcpy(tileCmdOut->vdp2CmdData, cmd->vdp2CmdData, sizeof(cmd->vdp2CmdData));

            uint32_t tileRenderStartTime = picocom_time_us_32();

            // run job in core1 ( in parallel with core 2 )
            memset( job->tileFrameBuffer.pixelsData, 0, sizeof(tileCmdOut->pixels) / 2);    
            
            // render gpu cmds                
            gpu_clear_error_stats(vdp->gpuState, &gpuInstance);
            gpu_begin_frame(vdp->gpuState, &gpuInstance, cmd->cmdSeqNum);
            gpu_run_tile(vdp->gpuState, &gpuInstance, (GpuCommandList_t*)&cmdList, &job->tileFrameBuffer);            // Render command into tile
            gpu_end_frame(vdp->gpuState, &gpuInstance);

            //picocom_sleep_ms(10);
            //END_PROFILE_BLOCK_US();
            //printf("\tvdp1_render_tile_job[%d] took: %d us (%d ms)\n", coreId, t1-t0, (t1-t0)/1000); 

            // build vdp2 cmd
            tileCmdOut->tileId = job->tileFrameBuffer.tileId;
            tileCmdOut->cmdSeqNum = cmd->cmdSeqNum;
            tileCmdOut->cmdFlags = tileCmdFlags;
            tileCmdOut->passId = cmd->passId;
            tileCmdOut->defaultBlendMode = cmd->defaultBlendMode;
            tileCmdOut->globalVdp2PalBufferId = job->cmdIn->globalVdp2PalBufferId;

            // add stats
            job->cmdErrors = gpuInstance.frameStats.cmdErrors;
            job->tileRenderTookTime = picocom_time_us_32() - tileRenderStartTime;                   
        } 
        break;
    }        
    default:
        break;
    }
}



void vdp1_core2_main() 
{
    // wait on job queue
    while(true)
    {     
        // wait job
        queue_entry_t entry;
        queue_remove_blocking(&g_vdp1->jobQueue, &entry);

        // render
        struct tileListJob_t* job = (struct tileListJob_t*)entry.job;
        vdp1_render_tile_job(g_vdp1, 1, job);

        // push result
        queue_entry_t result;
        result.job = job;
        queue_add_blocking(&g_vdp1->completeQueue, &result);        
    }
}


static void app_handler_main(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    //printf("[vdp1] app_handler_main %d\n", frame->cmd);

    struct vdp1_t* vdp = (struct vdp1_t*)bus->userData;

    switch(frame->cmd)
    {        
    case EBusCmd_VDP1_ResetBus:
    {
        bus_tx_reset(vdp->app_vlnk_tx);
        break;
    }
    case EBusCmd_VDP1_DebugDump:
    {
        // gpu dump
        printf("VDP1 gpu state\n");
        gpu_debug_dump_state(vdp->gpuState, false );

        // FWD to vdp2         
        VDP1CMD_DebugDump debugCmd = {};
        BUS_INIT_CMD(debugCmd, EBusCmd_VDP1_DebugDump);
      
        bus_tx_wait(vdp->vdp2_vdbus_tx);  
        bus_tx_write_async(vdp->vdp2_vdbus_tx, (uint8_t*)&debugCmd.header, sizeof(debugCmd));
        bus_tx_wait(vdp->vdp2_vdbus_tx);
        break;
    }
    case EBusCmd_VDP1_DrawCmdData:
    {        
        struct VDP1CMD_DrawCmdData* cmd = (struct VDP1CMD_DrawCmdData*)frame;
        //LOG_BUS_CMD("[vdp1] app_handler_main %d ([VDP1] EBusCmd_VDP1_DrawCmdData)\n", cmd->cmdSeqNum);

#ifdef VDP_VALIDATE_CMDS
        // Validate vdp1 main cmds
        GpuValidationOutput_t validator;
        if(!gpu_validate_cmds_list(vdp->gpuState, &cmdList, &validator))
        {
            gpu_dump_cmds(vdp->gpuState, &cmdList);
            gpu_print_validator_error( &validator );            
            //LOG_BUS_CMD("!cmds not valid\n");
        }

        // validate vdp2 cmds
        struct GpuCommandList_t cmdList2 = {0};
        cmdList2.headerSz = 0;
        cmdList2.allocSz = cmd->header.sz;
        cmdList2.offset = 0;
        cmdList2.cmdCount = cmd->vdp2CmdDataCount; 
        cmdList2.cmdData = cmd->vdp2CmdData;
        
        gpu_dump_cmds(0, &cmdList2);
#endif

        // get create tile buffer for gpu
        uint32_t cmdErrors = 0;
        uint32_t tileBusCopyTotalTime = 0;
        uint32_t tileRenderTotalTime = 0;

        switch (cmd->colorDepth)
        {
        case EColorDepth_BGR565:
        case EColorDepth_8BPP:
        {
#ifdef VDP1_MULTICORE_RENDER
            if(cmd->cmdDataCount > 0)
            {    
                // render striped on 2 cores
                for(int i=0;i<FRAME_TILE_CNT_Y;i++)
                {
                    if( (cmd->tileMask & (1 << i)) == 0 )
                        continue;               

                    struct tileListJob_t* job0 = &vdp->job0;    

                    memset(job0, 0, sizeof(struct tileListJob_t));
                    job0->tileId = i;
                    job0->subTileId = 0; 
                    job0->cmdIn = cmd;
                    job0->tileCmdOut = vdp->tileCmdOut[ vdp->currentTileCmdId ];

                    struct tileListJob_t* job1 = &vdp->job1;    

                    memset(job1, 0, sizeof(struct tileListJob_t));
                    job1->tileId = i;
                    job1->subTileId = 1;
                    job1->cmdIn = cmd;
                    job1->tileCmdOut = vdp->tileCmdOut[ vdp->currentTileCmdId ];

                    // run job 2 on core 2 while inline render job 1
                    queue_entry_t entry = {.job=job1};
                    queue_add_blocking(&vdp->jobQueue, &entry);                

                    // render
                    vdp1_render_tile_job(g_vdp1, 0, job0);
                    //vdp1_render_tile_job(g_vdp1, 1, job1); // inline render on this core

                    // add stats
                    cmdErrors += job0->cmdErrors;                
                    tileRenderTotalTime += job0->tileRenderTookTime;
                    tileBusCopyTotalTime += job0->tileBusCopyTookTime;               

                    queue_entry_t result = {.job=0};
                    queue_remove_blocking(&vdp->completeQueue, &result);    
                    assert(result.job == job1);
                    
                    if( (cmd->tileMask & (1 << job0->tileId)) != 0 )
                        vdp1_send_tile_job(g_vdp1, 0, job0);    

                    // next buffer while sending
                    vdp->currentTileCmdId++;
                    if(vdp->currentTileCmdId > 1)
                        vdp->currentTileCmdId = 0;

                    // add stats
                    cmdErrors += job1->cmdErrors;                
                    tileRenderTotalTime += job1->tileRenderTookTime;
                    tileBusCopyTotalTime += job1->tileBusCopyTookTime;               
                }                
            }
#else
            if(cmd->cmdDataCount > 0)
            {    
                for(int i=0;i<FRAME_TILE_CNT_Y;i++)
                {
                    if( (cmd->tileMask & (1 << i)) == 0 )
                        continue;               

                    struct tileListJob_t* job0 = &vdp->job0;    

                    memset(job0, 0, sizeof(struct tileListJob_t));
                    job0->tileId = i;
                    job0->subTileId = 0; 
                    job0->cmdIn = cmd;
                    job0->tileCmdOut = vdp->tileCmdOut[ vdp->currentTileCmdId ];

                    struct tileListJob_t* job1 = &vdp->job1;    

                    memset(job1, 0, sizeof(struct tileListJob_t));
                    job1->tileId = i;
                    job1->subTileId = 1;
                    job1->cmdIn = cmd;
                    job1->tileCmdOut = vdp->tileCmdOut[ vdp->currentTileCmdId ];
                    
                    // render
                    vdp1_render_tile_job(g_vdp1, 0, job0);
                    vdp1_render_tile_job(g_vdp1, 1, job1); // inline render on this core

                    // add stats
                    cmdErrors += job0->cmdErrors;                
                    tileRenderTotalTime += job0->tileRenderTookTime;
                    tileBusCopyTotalTime += job0->tileBusCopyTookTime;               

                    if( (cmd->tileMask & (1 << job1->tileId)) != 0 )
                        vdp1_send_tile_job(g_vdp1, 0, job1);    

                    if( (cmd->tileMask & (0 << job0->tileId)) != 0 )
                        vdp1_send_tile_job(g_vdp1, 0, job0);    

                    // next buffer while sending
                    vdp->currentTileCmdId++;
                    if(vdp->currentTileCmdId > 1)
                        vdp->currentTileCmdId = 0;

                    // add stats
                    cmdErrors += job1->cmdErrors;                
                    tileRenderTotalTime += job1->tileRenderTookTime;
                    tileBusCopyTotalTime += job1->tileBusCopyTookTime;               
                }                
            }
#endif
            break;
        }    
        default:
            break;
        }

        // notify completion
        static VDP1CMD_AckDrawCmdData res = {};
        BUS_INIT_CMD(res, EBusCmd_VDP1_AckDrawCmdData);        
        res.cmdSeqNum = cmd->cmdSeqNum;
        res.gpuErrors = cmdErrors;     
        res.tileBusCopyTotalTime = tileBusCopyTotalTime;  
        res.tileRenderTotalTime = tileRenderTotalTime; 
        
        bus_tx_rpc_set_return_main(vdp->app_vlnk_tx, &res.header, &res.header);           
        break; 
    }
    case EBusCmd_VDP1_ForwardVDP2CmdData:
    {
        struct VDP1CMD_DrawCmdData* cmd = (struct VDP1CMD_DrawCmdData*)frame;

        // wait bus                        
        bus_tx_wait(vdp->vdp2_vdbus_tx);  

        // write jobA to vdp2        
        bus_tx_write_async(vdp->vdp2_vdbus_tx, (uint8_t*)&cmd->header, cmd->header.sz);

        // wait bus                        
        bus_tx_wait(vdp->vdp2_vdbus_tx);

        // notify completion
        // NOTE: limited error & profiling, could wait for response from vdp2 but this adds insane complexity and hideously hard to track bugs when it goes (even more) wrong.
        static VDP1CMD_AckDrawCmdData res = {};
        BUS_INIT_CMD(res, EBusCmd_VDP1_AckDrawCmdData);        
        res.cmdSeqNum = cmd->cmdSeqNum;
        res.gpuErrors = 0;     
        res.tileBusCopyTotalTime = 0;
        res.tileRenderTotalTime = 0; 
        
        bus_tx_rpc_set_return_main(vdp->app_vlnk_tx, &res.header, &res.header);           
        break;         
    }
    case EBusCmd_VDP1_GetStatus:
    {
        struct VDP1CMD_GetStatus* cmd = (struct VDP1CMD_GetStatus*)frame;
        uint16_t actualCrc = bus_calc_msg_crc(frame);
        if( cmd->header.crc != actualCrc)
        {
            printf("crc fail\n");
            break;
        }

        static struct VDP1CMD_GetStatus status  = {};        
        update_status_cmd( vdp, &status );
        
        BUS_INIT_CMD(status, EBusCmd_VDP1_GetStatus);
        status.counter = cmd->counter;
        
        bus_tx_rpc_set_return_main(vdp->app_vlnk_tx, frame, &status.header);
        break;
    }    
    }
}


static void vdp2_handler_realtime(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    struct vdp1_t* vdp = (struct vdp1_t*)bus->userData;

    switch(frame->cmd)
    {  
    default:
    {
        bus_rx_push_defer_cmd(bus, frame);     
               
        break;
    }    
    }
}


static void vdp2_handler_main(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    struct vdp1_t* vdp = (struct vdp1_t*)bus->userData;
}


int vdp1_init(struct vdp1_t* vdp, struct vdp1InitOptions_t* options)
{
    if(vdp->gpuState)
        return SDKErr_Fail;

    vdp->vdp2_vdbus_tx = options->vdp2_vdbus_tx;
    vdp->vdp2_vdbus_tx->tx_ack_timeout = 0;  // no timeout
    vdp->vdp2_xlnk_rx = options->vdp2_xlnk_rx;    
    vdp->vdp2_xlnk_rx->userData = vdp;

    vdp->app_vlnk_tx = options->app_vlnk_tx;
    vdp->app_vlnk_rx = options->app_vlnk_rx;  
    vdp->app_vlnk_rx->userData = vdp;

    bus_rx_set_callback(vdp->app_vlnk_rx, app_handler_realtime, app_handler_main);
    bus_rx_set_callback(vdp->vdp2_xlnk_rx, vdp2_handler_realtime, vdp2_handler_main);

    mutex_init(&vdp->sendLock);
    queue_init(&vdp->jobQueue, sizeof(queue_entry_t), 2);
    queue_init(&vdp->completeQueue, sizeof(queue_entry_t), 2);

    // alloc gpu
    vdp->gpuState = gpu_init(&options->gpuOptions);
    vdp->gpuState->debugName = "vdp1";
    if(!vdp->gpuState)
        return SDKErr_Fail;
    
    // alloc vdp2 command fwd buffer
    vdp->vdp2CmdFwdWriteBufferSz = options->vdp2CmdFwdWriteBufferSz;
    if(options->vdp2CmdFwdWriteBufferSz)
        vdp->vdp2CmdFwdWriteBuffer = picocom_malloc(vdp->vdp2CmdFwdWriteBufferSz);

    // alloc tile out cmds, pick largest size as buffer is re-used for all color modes
    int allocSize = MAX(sizeof(VDP2CMD_TileFrameBuffer16bpp),sizeof(VDP2CMD_TileFrameBuffer8bpp));
    for(int i=0;i<NUM_ELEMS(vdp->tileCmdOut);i++)
    {
        vdp->tileCmdOut[i] = picocom_malloc(allocSize);
        if(!vdp->tileCmdOut[i])
            return SDKErr_Fail;
    }

    return SDKErr_OK;
}


int vdp1_deinit(struct vdp1_t* vdp)
{
    if(vdp->gpuState)
        gpu_deinit(vdp->gpuState);    

    // clear state
    vdp->gpuState = 0;
    vdp->vdp2_vdbus_tx = 0;
    vdp->vdp2_xlnk_rx = 0;    
    vdp->app_vlnk_tx = 0;
    vdp->app_vlnk_rx = 0; 

    return SDKErr_OK;
}


int vdp1_update(struct vdp1_t* vdp, struct vdp1MainLoopOptions_t* options)
{
    // process rx queues
    bus_rx_update(vdp->app_vlnk_rx);
    bus_rx_update(vdp->vdp2_xlnk_rx);

    // process tx queues
    bus_tx_update(vdp->vdp2_vdbus_tx);
    bus_tx_update(vdp->app_vlnk_tx);
    return 1;
}


int vdp1_main(struct vdp1_t* vdp, struct vdp1MainLoopOptions_t* options)
{
#ifndef PICOCOM_NATIVE_SIM    
    // run core2 job loop
    picocom_multicore_launch_core1(vdp1_core2_main);
#endif
    
    // update loop (if options set)
    while(1)
    {
        // process rx queues
        bus_rx_update(vdp->app_vlnk_rx);
        bus_rx_update(vdp->vdp2_xlnk_rx);

        // process tx queues
        bus_tx_update(vdp->vdp2_vdbus_tx);
        bus_tx_update(vdp->app_vlnk_tx);
          
#ifdef PICOCOM_SDL        
        picocom_sleep_us(1);
#endif        
    }

    return 0;
}
