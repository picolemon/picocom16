#pragma once

/** Low level APU cmd interface. 
*/
#include "platform/pico/bus/bus.h"
#include "gpu/gpu_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Config
#define VDP1_STATUS_PUBLISH_INTERVAL_US     30000           // default 30ms status publish interval
#define VDP2_TILE_CMD_DATA_SZ 768                           // Fixed cmd list data for vdp2 comp cmds, used to composite tiles and draw backgrounds
#define VDP2_TILE_CMD_8BPP_DATA_SZ 128                      // Small cmd buffer for 8bpp mode

/** VDP1 Bus commands
 */
enum EBusCmd_VDP1
{    
    EBusCmd_VDP1_GetStatus = EBusCmd_VDP1_BASE,    
    EBusCmd_VDP1_GetConfig,
    EBusCmd_VDP1_SetConfig,
    EBusCmd_VDP1_DrawCmdData,
    EBusCmd_VDP1_ForwardVDP2CmdData,
    EBusCmd_VDP1_AckDrawCmdData,
    EBusCmd_VDP1_GpuFrameStats,
    EBusCmd_VDP1_GpuProfileStats,
    EBusCmd_VDP1_ResetBus,
    EBusCmd_VDP1_DebugDump,
};


/** VDP state  */
enum EVDP1State {
    EVDP1State_Idle,
    EVDP1State_waitVDP2Idle,   // Stall wait for VDP2 completion
    EVDP1State_writeVDP2Data,   // Write VDP2 data from cmd data
    EVDP1State_renderVDP1Tile,  // Render VDP1 (if any)
    EVDP1State_writeVDP1Tile,   // Write VDP1 tile to VDP2 for compositing (if any)
    EVDP1State_waitVDP2CompleteTile,    // Wait for VDP2 comp into back buffer
    EVDP1State_Done,
};


/** Get vdp1 status */
typedef struct __attribute__((__packed__)) VDP1CMD_GetStatus
{
    uint32_t counter;       // Ping counter
    uint8_t isOnline;       // VDP1 online
    uint8_t vdpState;       // [EVDP1State] State of vdp    
    Cmd_Header_t header; 
} VDP1CMD_getStatus;


/** VDP1 config */
typedef struct __attribute__((__packed__)) VDP1CMD_Config
{
    Cmd_Header_t header; 
    bool profilerEnabled;
    uint32_t profilerLevel;
} VDP1CMD_Config;


/** VDP1 config */
typedef struct __attribute__((__packed__)) VDP1CMD_DebugDump
{
    Cmd_Header_t header; 
} VDP1CMD_DebugDump;


/** VDP1CMD_DrawCmdData cmdFlags  */
enum EVDP1CMD_DrawCmdData_completeFlags
{
    EVDP1CMD_DrawCmdData_completeFlags_None = 0,    
    EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay = 1 << 0,        // Flip display buffer
    EVDP1CMD_DrawCmdData_completeFlags_WriteVDP2Tile = 1 << 1,      // VDP1 write tile to VDP2, exlclude for vdp1 non-display/uploads
    EVDP1CMD_DrawCmdData_completeFlags_CopyFB = 1 << 2,             // Copy framebuffer to front no sync ( useful for debug )    
};


/** Low level draw command data */
typedef struct __attribute__((__packed__)) VDP1CMD_DrawCmdData
{
    Cmd_Header_t header;    

    uint32_t cmdSeqNum;             // Unique cmd id    
    uint16_t tileMask;              // Select tiles to render
    uint32_t cmdFlags;              // Command flag eg. flip display
    uint8_t colorDepth;             // [EColorDepth] 16 or 8 bpp
    uint16_t globalVdp2PalBufferId; // Palette for 8bpp modes, uploaded to vdp2 comp phase
        
    uint32_t passId;                // Padd index
    uint8_t defaultBlendMode;       // VDP2 Zero pipeline default comp blend

    // Cmd data to exec        
    uint16_t vdp2CmdDataCount;      // Command cnt (cmdData CmdList total cmds)    
    uint32_t vdp2CmdDataSz;         // Size of cmd data ( malloc cmd sizeof(VDP1CMD_DrawCmdData) + cmdDataSz )               
    uint8_t vdp2CmdData[VDP2_TILE_CMD_DATA_SZ]; // VDP2 cmd data

    // Cmd data to exec        
    uint16_t cmdDataCount;          // Command cnt (cmdData CmdList total cmds)    
    uint32_t cmdDataSz;             // Size of cmd data ( malloc cmd sizeof(VDP1CMD_DrawCmdData) + cmdDataSz )                 
    uint8_t cmdData[0];             // [must be last entry] command data struct (data size command->sz - offsetof(cmdData))
    // [DONT ADD MEMBERS]
} VDP1CMD_DrawCmdData;



/** Ack for draw cmd data */
typedef struct __attribute__((__packed__)) VDP1CMD_AckDrawCmdData
{
    Cmd_Header_t header;   

    uint32_t cmdSeqNum;             // Unique cmd id
    uint32_t gpuErrors;             // gpu error count
    uint32_t tileBusCopyTotalTime;  // stat for total copy time
    uint32_t tileRenderTotalTime;   // stat for tile render time 
} VDP1CMD_AckDrawCmdData;


/** Get vdp1/2 frame stats */
typedef struct __attribute__((__packed__)) VDP1CMD_GpuFrameStats
{    
    Cmd_Header_t header; 
    uint8_t vdpId;
    GpuFrameStats_t frameStats;
} VDP1CMD_GpuFrameStats;


/** Get vdp1/2 frame profile */
typedef struct __attribute__((__packed__)) VDP1CMD_GpuFrameProfile
{    
    Cmd_Header_t header; 
    uint8_t vdpId;
    GpuFrameProfile frameProfile;
} VDP1CMD_GpuFrameProfile;


/** Init draw command data size from cmd list ( variable sized cmds with offsets applied )
    VDP1CMD_DrawCmdData cmdOut  - target command to send
    GpuCommandList cmdList      - source command list     
*/
#define INIT_VDP1CMD_DrawCmdData_cmdlist_sz(cmdOut, cmdList) \
    BUS_INIT_CMD_PTR(cmdOut, EBusCmd_VDP1_DrawCmdData); \
    cmdOut->header.sz = cmdList->offset; \
    cmdOut->cmdDataCount = cmdList->cmdCount; \
    cmdOut->cmdDataSz = cmdList->offset - cmdList->headerSz; \

    
#ifdef __cplusplus
}
#endif
