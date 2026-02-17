#include "vdp_client.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#ifdef PICOCOM_NATIVE_SIM
    // Fwd
    void test_service_vdp1_main(void* userData);
#endif

// tracer helper
#ifdef VDP1_DEBUG_TRACE_BUS_CMDS
    #define LOG_BUS_CMD printf

#else
    #define LOG_BUS_CMD(...) \

#endif

// Fwd
#ifdef PICOCOM_NATIVE_SIM
    void test_core_vdp1_update(void* userData);
    void  test_core_vdp2_update();
#endif    

//
//
static void vdp1_handler_realtime(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{    
    struct VdpClientImpl_t* client = (struct VdpClientImpl_t*)bus->userData;

    switch(frame->cmd)
    {
        default:
        {
            bus_rx_push_defer_cmd(bus, frame);            
            break;
        }
    }
}


static void vdp1_handler_main(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{    
    struct VdpClientImpl_t* client = (struct VdpClientImpl_t*)bus->userData;

    switch(frame->cmd)
    {
        case EBusCmd_VDP1_AckDrawCmdData:
        {
            struct VDP1CMD_AckDrawCmdData* cmd = (struct VDP1CMD_AckDrawCmdData*)frame;

            // iter over requests & mark its status
            bool found = false;
            for(int i=0;i<client->pendingCmds.count;i++)
            {
                struct VdpPendingVDPCmd_t* elem;
                if(!heap_array_get_value(&client->pendingCmds, i, (uint8_t*)&elem, sizeof(elem)))
                    break;
                
                if(elem->cmdSeqNum == cmd->cmdSeqNum)
                {
                    LOG_BUS_CMD("[app] handle complete cmdSeqNum: %d, vdpId: %d\n", cmd->cmdSeqNum, elem->vdpId);
                    found = true;
                    // mark completed
                    if(elem->state == EVDP1CmdSumbmitState_Queued)
                    {
                        elem->state = EVDP1CmdSumbmitState_NotifyCompletion;

                        // copy gpu exec stats
                        elem->gpuErrors = cmd->gpuErrors;

                        // stats
                        client->tileBusCopyTotalTime += cmd->tileBusCopyTotalTime;
                        client->tileRenderTotalTime += cmd->tileRenderTotalTime;
                    }
                }
            }

            if(!found)
                LOG_BUS_CMD("[app] unkonwn handle complete %d\n", cmd->cmdSeqNum);

            break;
        }
        default:
        {
            bus_rx_push_defer_cmd(bus, frame);            
            break;
        }
    }
}


struct VdpClientImpl_t* vdp1_client_init(struct VdpClientInitOptions_t* options)
{
    //mem_begin_profile("vdp1_client_init");

    struct VdpClientImpl_t* client = (struct VdpClientImpl_t*)picocom_malloc(sizeof(struct VdpClientImpl_t));
    if(!client)
        return 0;
    memset(client, 0, sizeof(struct VdpClientImpl_t));

    if(!heap_array_init(&client->cmdLists, sizeof(struct GpuCommandList_t*), options->cmdListPoolCnt))
        return 0;

    if(!heap_array_init(&client->freeCmdLists, sizeof(struct GpuCommandList_t*), options->cmdListPoolCnt))
        return 0;

    if(!heap_array_init(&client->pendingCmds, sizeof(struct VdpPendingVDPCmd_t*), options->cmdListPoolCnt))
        return 0;

    if(!heap_array_init(&client->freePendingCmds, sizeof(struct VdpPendingVDPCmd_t*), options->cmdListPoolCnt))
        return 0;

    for(int i=0;i<options->cmdListPoolCnt;i++)
    {
        struct GpuCommandList_t* cmdList = gpu_cmd_list_init(options->cmdListAllocSize, sizeof(struct VDP1CMD_DrawCmdData));
        if(!cmdList)
            return 0;
        if(!heap_array_append(&client->cmdLists, (uint8_t*)&cmdList, sizeof(struct GpuCommandList_t*)) )
            return 0;
        if(!heap_array_append(&client->freeCmdLists, (uint8_t*)&cmdList, sizeof(struct GpuCommandList_t*)) )
            return 0; 

        struct VdpPendingVDPCmd_t* pendingCmd = picocom_malloc(sizeof(VdpPendingVDPCmd_t));
        if(!pendingCmd)
            return 0;
        if(!heap_array_append(&client->freePendingCmds, (uint8_t*)&pendingCmd, sizeof(struct VdpPendingVDPCmd_t*)) )
            return 0;             
    }

    client->maxCmdSize = options->cmdListAllocSize - sizeof(struct VDP1CMD_DrawCmdData);
    client->vdp1Link_tx = options->vdp1Link_tx;
    client->vdp1Link_rx = options->vdp1Link_rx;
    
    // set rx handler
    client->vdp1Link_rx->userData = client;
    bus_rx_set_callback(client->vdp1Link_rx, vdp1_handler_realtime, vdp1_handler_main);
    
    client->defaultVDP2CompBlendMode = EBlendMode_Alpha;    // Default tiler uses alpha blending to overlay multiple passes

    //mem_end_profile("vdp1_client_init");

    return client;
}


int vdp1_client_get_status(struct VdpClientImpl_t* client, struct VDP1CMD_GetStatus* statusOut)
{
    // blocking status query    
    int res;
    struct VDP1CMD_GetStatus statusCmd = {};    
    statusCmd.vdpState = 69;
    BUS_INIT_CMD(statusCmd, EBusCmd_VDP1_GetStatus);
#ifdef PICOCOM_NATIVE_SIM    
    res = bus_tx_request_blocking_ex(client->vdp1Link_tx, client->vdp1Link_rx, &statusCmd.header, &statusOut->header, sizeof(*statusOut), 10000, test_service_vdp1_main, 0 );
#else
    res = bus_tx_request_blocking_ex(client->vdp1Link_tx, client->vdp1Link_rx, &statusCmd.header, &statusOut->header, sizeof(*statusOut), 10000, 0, 0 );    
#endif
    if(res != SDKErr_OK)
    {
        return SDKErr_Fail;
    }

    return SDKErr_OK;
}


int vdp1_client_wait_free(struct VdpClientImpl_t* client)
{
    while(client->freeCmdLists.count <= 0)
    {
        vdp1_update_queue(client);        
        tight_loop_contents();
    }
    return client->freeCmdLists.count;
}


struct VdpPendingVDPCmd_t* vdp1_client_begin_cmd_list(struct VdpClientImpl_t* client )
{
    // get free from pool, poll this until
    if(client->freeCmdLists.count <= 0)
        return 0;

    // Ensure synced
    if(client->freePendingCmds.count != client->freeCmdLists.count)
        return 0;

    // Ensure room in pending
    if(client->pendingCmds.count >= client->freeCmdLists.capacity)
        return 0;

    struct GpuCommandList_t* cmdList;
    if(!heap_array_get_value(&client->freeCmdLists, client->freeCmdLists.count - 1, (uint8_t*)&cmdList, sizeof(cmdList)))
        return 0;        
    if(!cmdList)
        return 0;

    struct VdpPendingVDPCmd_t* result;
    if(!heap_array_get_value(&client->freePendingCmds, client->freePendingCmds.count - 1, (uint8_t*)&result, sizeof(result)))
        return 0;        
    if(!result)
        return 0;        

    // pop
    client->freeCmdLists.count--;
    client->freePendingCmds.count--;

    // add to pending queue
    heap_array_append(&client->pendingCmds, (uint8_t*)&result, sizeof(result));

    // build req info
    result->cmdList = cmdList;
    result->cmdSeqNum = client->last_cmdSeqNum++;
    result->state = EVDP1CmdSumbmitState_Queued;
    
    // Create VDP2 cmd list from static section in VDP1CMD_DrawCmdData
    {
        struct VDP1CMD_DrawCmdData* cmdOut = (VDP1CMD_DrawCmdData*)cmdList->cmdData;
        cmdOut->vdp2CmdDataCount = 0; // init zero
        cmdOut->vdp2CmdDataSz = 0; // init zero

        // Construct ref cmd list
        result->refVdp2CmdList.headerSz = 0;
        result->refVdp2CmdList.allocSz = sizeof(cmdOut->vdp2CmdData); // ref cmd data size in VDP1CMD_DrawCmdData
        result->refVdp2CmdList.offset = 0;
        result->refVdp2CmdList.cmdCount = 0;
        result->refVdp2CmdList.cmdData = cmdOut->vdp2CmdData; // Reference cmd data in VDP1CMD_DrawCmdData
    }

    // prepare cmd list
    gpu_cmd_list_clear(cmdList);

    return result;
}


int vdp1_client_commit_cmd_list(struct VdpClientImpl_t* client, struct VdpPendingVDPCmd_t* req, uint16_t tileMask, uint32_t cmdFlags)
{
    // ensure pooled not some random alloced
    bool found = false;
    for(int i=0;i<client->cmdLists.count;i++)
    {
        struct VdpPendingVDPCmd_t* elem;
        if(!heap_array_get_value(&client->pendingCmds, i, (uint8_t*)&elem, sizeof(elem)))
            break;
        if(elem == req)
        {
            found = true;
            break;
        }
    }
    if(!found) 
        return 0; // Not tracked by pool
        
    // write cmdlist to vdp    
    struct GpuCommandList_t* cmdList = req->cmdList;
    struct VDP1CMD_DrawCmdData* cmdOut = (VDP1CMD_DrawCmdData*)cmdList->cmdData;
    assert(cmdList->headerSz == sizeof(VDP1CMD_DrawCmdData));
    INIT_VDP1CMD_DrawCmdData_cmdlist_sz(cmdOut, cmdList);  // size packet to cmdlist    
    cmdOut->tileMask = tileMask;
    cmdOut->cmdFlags = cmdFlags;
    cmdOut->cmdSeqNum = ++req->cmdSeqNum;
    cmdOut->passId = req->passId;
    cmdOut->defaultBlendMode = client->defaultVDP2CompBlendMode;
    cmdOut->colorDepth = client->colorDepth;
    cmdOut->globalVdp2PalBufferId = client->globalVdp2PalBufferId;

    // commit vdp2 data
    cmdOut->vdp2CmdDataCount = req->refVdp2CmdList.cmdCount;
    cmdOut->vdp2CmdDataSz = req->refVdp2CmdList.offset;
    
    req->submitTime = picocom_time_us_64();
    
    // queue on bus for tx
    LOG_BUS_CMD("[app] write cmd cmdSeqNum: %d\n", cmdOut->cmdSeqNum);
    bus_tx_write_cmd_async(client->vdp1Link_tx, &cmdOut->header);

    return 1;
}


int vdp1_client_is_completed(struct VdpClientImpl_t* client, struct VdpPendingVDPCmd_t* req)
{
    if(!req)
        return 1;

    // check if cmd was acked
    if(req->state == EVDP1CmdSumbmitState_Queued)
        return 0;

    // default 1 for unknown or just not-running, completed doesnt mean success
    return 1;
}

int vdp1_client_wait_completion(struct VdpClientImpl_t* client, struct VdpPendingVDPCmd_t* req, uint32_t timeoutMs)
{
    // block while waiting
    while(1)
    {
        // check before update
        if(req->state == EVDP1CmdSumbmitState_Completed)
            return 1;

        vdp1_update_queue(client);        
        tight_loop_contents();      
    }

    return 0;
}


int vdp1_update_queue(struct VdpClientImpl_t* client)
{
    bus_rx_update(client->vdp1Link_rx);
    bus_tx_update(client->vdp1Link_tx);

#ifdef PICOCOM_NATIVE_SIM
    // Service vdp 1 
    test_core_vdp1_update(0);
    test_core_vdp2_update();
#endif    

    // iter over requests & mark its status    
    for(int i=0;i<client->pendingCmds.count;i++)
    {
        struct VdpPendingVDPCmd_t* elem;
        if(!heap_array_get_value(&client->pendingCmds, i, (uint8_t*)&elem, sizeof(elem)))
            break;
        
        // free completed
        if(elem->state == EVDP1CmdSumbmitState_NotifyCompletion)
        {
            elem->state = EVDP1CmdSumbmitState_Completed;
        }
        else if(elem->state == EVDP1CmdSumbmitState_Completed)
        {
            // clear mem state
            elem->state = EVDP1CmdSumbmitState_None;    

            // remove elem
            heap_array_remove_and_swap(&client->pendingCmds, i); 

            // add cmd list
            // TODO: do checks
            if(elem->cmdList)
            {
                if(!heap_array_append(&client->freeCmdLists, (uint8_t*)&elem->cmdList, sizeof(struct VdpPendingVDPCmd_t*)) )
                    return 0;     
            }

            // push into pool
            // TODO: do checks
            if(!heap_array_append(&client->freePendingCmds, (uint8_t*)&elem, sizeof(struct VdpPendingVDPCmd_t*)) )
                return 0;     

            // 1 per update
            return 1;
        }
    }    

    return 0;
}


int vdp1_begin_frame(struct VdpClientImpl_t* client, uint16_t tileMask, uint32_t cmdFlags)
{
    // force complete
    if(client->currentPending)
    {
        client->currentPending->state = EVDP1CmdSumbmitState_Completed; // TODO: add cancel mode to not trigger any side effects
        vdp1_update_queue(client);
        client->currentPending = 0;
    }

    client->current_tileMask = tileMask;
    client->current_cmdFlags = cmdFlags;
    client->pendingCmdBufferOverflowCnt = 0;
    client->tileBusCopyTotalTime = 0;
    client->tileRenderTotalTime = 0;
    client->autoFlush_cmdFlag = EVDP1CMD_DrawCmdData_completeFlags_WriteVDP2Tile;
    client->colorDepth = EColorDepth_BGR565;
    
    return SDKErr_OK;
}


GpuCmd_Header* vdp1_cmd_add_next(struct VdpClientImpl_t* client, uint8_t vdpIdSection, uint32_t sz)
{
    return vdp1_cmd_add_next_impl( client, vdpIdSection, sz, true ); // allow flush by default
}


GpuCmd_Header* vdp1_cmd_add_next_impl(struct VdpClientImpl_t* client, uint8_t vdpIdSection, uint32_t sz, bool canFlush)
{
    if(vdpIdSection == 0)
    {
        struct VdpPendingVDPCmd_t* cmdReq = client->currentPending;
        
        // check can add
        if(!cmdReq || !gpu_cmd_list_can_add(cmdReq->cmdList, sz))
        {
            // flush current                
            if(cmdReq)
            {      
                // req from begin is full but auto flush is disabled, called must check this before adding new cmds.
                // Abort if cant flush to vdp
                if(!canFlush)
                    return 0;

                vdp1_client_commit_cmd_list(client, cmdReq, client->current_tileMask, client->autoFlush_cmdFlag);

                vdp1_update_queue(client);

                vdp1_client_wait_completion(client, cmdReq, 0);

                client->pendingCmdBufferOverflowCnt++;
            }

            // wait gpu buffer free after flush
            vdp1_client_wait_free(client);  // wait until for free data in steam

            // get next cmd block
            cmdReq = vdp1_client_begin_cmd_list( client );
            if(!cmdReq)
                return 0;

            cmdReq->passId = client->pendingCmdBufferOverflowCnt; // bind pass index
            client->currentPending = cmdReq;
        }

        if(!cmdReq->cmdList)
            return 0;

        return gpu_cmd_list_add_next(cmdReq->cmdList, sz);  
    }
    else if(vdpIdSection == 1)
    {
        struct VdpPendingVDPCmd_t* cmdReq = client->currentPending;
        if(!cmdReq)
        {
            // wait gpu buffer free after flush
            vdp1_client_wait_free(client);  // wait until for free data in steam

            // get next cmd block
            cmdReq = vdp1_client_begin_cmd_list( client );
            if(!cmdReq)
                return 0;

            cmdReq->passId = 0; // Single pass only
            client->currentPending = cmdReq; 
        }

        return gpu_cmd_list_add_next(&cmdReq->refVdp2CmdList, sz);  
    }
    return 0;
}


int vdp1_end_frame(struct VdpClientImpl_t* client, bool waitCompletion, bool writeVDP2Tile)
{
    int result = SDKErr_OK;

    if(writeVDP2Tile)
        client->current_cmdFlags |= EVDP1CMD_DrawCmdData_completeFlags_WriteVDP2Tile;

    struct VdpPendingVDPCmd_t* cmdReq = client->currentPending;
    //printf("write tile %d, writeVDP2Tile: %d, passId: %d, cmdCount:  %d\n", cmdReq->cmdSeqNum, writeVDP2Tile, cmdReq->passId, cmdReq->refVdp2CmdList.cmdCount);
    if(cmdReq)
    {        
        vdp1_client_commit_cmd_list(client, cmdReq, client->current_tileMask, client->current_cmdFlags);
    }

    vdp1_update_queue(client);

    if(waitCompletion)
    {
        if(cmdReq)
        {
            vdp1_client_wait_completion(client, cmdReq, 0);

            if(cmdReq->gpuErrors > 0)
            {
                LOG_BUS_CMD("Gpu cmd req %d errors %d detected\n", cmdReq->cmdSeqNum, cmdReq->gpuErrors);
                result = SDKErr_Fail;
            }
        }
    }

    client->currentPending = 0;
    client->current_tileMask = 0;
    client->current_cmdFlags = 0;
    return result;
}



int vdp1_end_frame_commit_to_vdp2(struct VdpClientImpl_t* client, bool waitCompletion)
{
    int result = SDKErr_OK;

    struct VdpPendingVDPCmd_t* cmdReq = client->currentPending;
    if(cmdReq)
    {        
        vdp1_client_commit_cmd_list_to_vdp2(client, cmdReq, client->current_tileMask, client->current_cmdFlags);
    }

    vdp1_update_queue(client);

    if(waitCompletion)
    {
        if(cmdReq)
        {
            vdp1_client_wait_completion(client, cmdReq, 0);

            if(cmdReq->gpuErrors > 0)
            {
                LOG_BUS_CMD("Gpu cmd req %d errors %d detected\n", cmdReq->cmdSeqNum, cmdReq->gpuErrors);
                result = SDKErr_Fail;
            }
        }
    }

    client->currentPending = 0;
    client->current_tileMask = 0;
    client->current_cmdFlags = 0;
    return result;
}


int vdp1_client_commit_cmd_list_to_vdp2(struct VdpClientImpl_t* client, struct VdpPendingVDPCmd_t* req, uint16_t tileMask, uint32_t cmdFlags)
{
    // ensure pooled not some random alloced
    bool found = false;
    for(int i=0;i<client->cmdLists.count;i++)
    {
        struct VdpPendingVDPCmd_t* elem;
        if(!heap_array_get_value(&client->pendingCmds, i, (uint8_t*)&elem, sizeof(elem)))
            break;
        if(elem == req)
        {
            found = true;
            break;
        }
    }
    if(!found) 
        return 0; // Not tracked by pool
        
    // write cmdlist to vdp    
    struct GpuCommandList_t* cmdList = req->cmdList;
    struct VDP1CMD_DrawCmdData* cmdOut = (VDP1CMD_DrawCmdData*)cmdList->cmdData;
    assert(cmdList->headerSz == sizeof(VDP1CMD_DrawCmdData));
    INIT_VDP1CMD_DrawCmdData_cmdlist_sz(cmdOut, cmdList);  // size packet to cmdlist    
    cmdOut->header.cmd = EBusCmd_VDP1_ForwardVDP2CmdData; // Override cmd id, vdp1 has special forwarder service
    cmdOut->tileMask = tileMask;
    cmdOut->cmdFlags = cmdFlags;
    cmdOut->cmdSeqNum = ++req->cmdSeqNum;
    cmdOut->passId = req->passId;
    cmdOut->defaultBlendMode = client->defaultVDP2CompBlendMode;

    // commit vdp2 data
    cmdOut->vdp2CmdDataCount = req->refVdp2CmdList.cmdCount;
    cmdOut->vdp2CmdDataSz = req->refVdp2CmdList.offset;
    
    // queue on bus for tx
    LOG_BUS_CMD("[app] write cmd cmdSeqNum: %d\n", cmdOut->cmdSeqNum);
    bus_tx_write_cmd_async(client->vdp1Link_tx, &cmdOut->header);

    return 1;
}
