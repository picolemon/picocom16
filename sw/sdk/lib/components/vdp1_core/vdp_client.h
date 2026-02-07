#pragma once

#include "lib/components/vdp1_core/vdp_client.h"
#include "picocom/devkit.h"
#include "lib/components/vdp2_core/vdp2_core.h"
#include "picocom/display/display.h"
#include "platform/pico/hw/picocom_hw.h"
#include "platform/pico/vdp1/hw_vdp1_types.h"
#include "picocom/utils/array.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>


/* Command submit state */
enum EVDP1CmdSumbmitState 
{
    EVDP1CmdSumbmitState_None,
    EVDP1CmdSumbmitState_Queued,        // Bus queued        
    EVDP1CmdSumbmitState_NotifyCompletion, // Bus notify completion
    EVDP1CmdSumbmitState_Completed,     // VDP acked cmd exec completed
};


/** Pending cmd info */
typedef struct VdpPendingVDPCmd_t
{
    const char* name;                               // debug name
    uint32_t cmdSeqNum;                             // Unique cmd seq num
    uint32_t submitTime;                            // Time client submitting cmd
    struct GpuCommandList_t* cmdList;                 // Cmd list data sent (pooled in freeCmdLists)
    struct GpuCommandList_t refVdp2CmdList;           // VDP2 static cmd list, data members point to VDP1CMD_DrawCmdData.vdp2CmdData data section
    enum EVDP1CmdSumbmitState state;                // State
    uint32_t gpuErrors;                             // gpu error count    
    uint8_t passId;                                 // pass index in frame ( after begin frame )
} VdpPendingVDPCmd_t;


/** VDP client init options */
typedef struct VdpClientInitOptions_t {
    struct BusTx_t* vdp1Link_tx;
    struct BusRx_t* vdp1Link_rx;        
    uint32_t cmdListPoolCnt;        // number of cmd lists to allow in flight
    uint32_t cmdListAllocSize;      // cmd list size sould match max bus packet    
} VdpClientInitOptions_t;


/** VDP1 client */
typedef struct VdpClientImpl_t
{
    struct BusTx_t* vdp1Link_tx;
    struct BusRx_t* vdp1Link_rx;    

    struct heap_array_t cmdLists;                       // [GpuCommandList_t*] all allocated cmd lists
    struct heap_array_t pendingCmds;                    // [VdpPendingVDPCmd_t*] pending cmds, acked for completion notify and returns free list

    struct heap_array_t freePendingCmds;                // [VdpPendingVDPCmd_t*] pending cmd pool
    struct heap_array_t freeCmdLists;                   // [GpuCommandList_t*] free pool    
    
    uint32_t last_cmdSeqNum;        

    // Immediate state
    struct VdpPendingVDPCmd_t* currentPending;          
    uint16_t current_tileMask;
    uint32_t current_cmdFlags;
    uint32_t autoFlush_cmdFlag;             // Flags to set when forcing flush during cmd add, defaults to flush tile and comp
    uint32_t pendingCmdBufferOverflowCnt;   // stall count when cmd buffer overflows, waits for flush
    uint8_t colorDepth;                     // [EColorDepth] 16 or 8 bpp
    uint16_t globalVdp2PalBufferId;         // Palette for 8bpp modes, uploaded to vdp2 comp phase

    // Frame stats (non profiling)
    uint32_t tileBusCopyTotalTime;      // stat for total copy time
    uint32_t tileRenderTotalTime;       // stat for tile render time client    

    uint32_t maxCmdSize;                // max possible gpu cmd

    uint8_t defaultVDP2CompBlendMode;   // Default vdp2 tile comp mode (defaults to alpha for for comping tiles with alpha data)
} VdpClientImpl_t;


// VDP1 client core
struct VdpClientImpl_t* vdp1_client_init(struct VdpClientInitOptions_t* options);
int vdp1_client_get_status(struct VdpClientImpl_t* client, struct VDP1CMD_GetStatus* statusOut);
int vdp1_client_wait_free(struct VdpClientImpl_t* client);
struct VdpPendingVDPCmd_t* vdp1_client_begin_cmd_list(struct VdpClientImpl_t* client );
int vdp1_client_commit_cmd_list(struct VdpClientImpl_t* client, struct VdpPendingVDPCmd_t* cmd, uint16_t tileMask, uint32_t cmdFlags);
int vdp1_client_is_completed(struct VdpClientImpl_t* client, struct VdpPendingVDPCmd_t* req);
int vdp1_client_wait_completion(struct VdpClientImpl_t* client, struct VdpPendingVDPCmd_t* req, uint32_t timeoutMs);
int vdp1_update_queue(struct VdpClientImpl_t* client);

// frame and command api
int vdp1_begin_frame(struct VdpClientImpl_t* client, uint16_t tileMask, uint32_t cmdFlags);
GpuCmd_Header* vdp1_cmd_add_next(struct VdpClientImpl_t* client, uint8_t vdpIdSection, uint32_t sz); // Add command & alloc next cmd, add cmds in main vdp1 or vdp2 smaller payload (not this is bound per tile)
GpuCmd_Header* vdp1_cmd_add_next_impl(struct VdpClientImpl_t* client, uint8_t vdpIdSection, uint32_t sz, bool canFlush); // add cmd impl
int vdp1_end_frame(struct VdpClientImpl_t* client, bool waitCompletion, bool writeVDP2Tile);        // end frame, writeVDP2Tile will generate VDP2 tiles for compositing to back buffer.

// special api for vdp access
int vdp1_end_frame_commit_to_vdp2(struct VdpClientImpl_t* client, bool waitCompletion);                                                         // Flush command to vdp2 ( via vdp2 forwarding )
int vdp1_client_commit_cmd_list_to_vdp2(struct VdpClientImpl_t* client, struct VdpPendingVDPCmd_t* cmd, uint16_t tileMask, uint32_t cmdFlags); // Commit command to vdp2 ( via vdp2 forwarding )