/** Low level APU cmd interface. 
*/
#pragma once

#include "platform/pico/bus/bus.h"
#include "platform/pico/vdp1/hw_vdp1_types.h" // For draw command data
#include <stdint.h>


// Constants
#define VDP_TILEFRAME_BUFFER_Y_SIZE 48                          // Tile rendering block size (render & sync block size)
#define GPU_MAX_BUFFER  128                                     // Max buffers gpu can store
#define VDP2_TILE_RENDER_POOL_SZ 2                              // Tile render queue size
#define VDP2_TILE_WRITER_QUEUE_SZ 2
#define TILE_CNT_Y (FRAME_H/VDP_TILEFRAME_BUFFER_Y_SIZE)        // Tile count split based on VDP2CMD_TileFrameBuffer transfer size (determins render size)


/** VDP Bus commands
 */
enum EBusCmd_VDP2
{    
    EBusCmd_VDP2_GetStatus = EBusCmd_VDP2_BASE,
    EBusCmd_VDP2_Get_Config,
    EBusCmd_VDP2_Set_Config,
    EBusCmd_VDP2_TileFrameBuffer16bpp,    
    EBusCmd_VDP2_TileFrameBuffer8bpp,    
    EBusCmd_VDP2_DrawCmdData, // uses hw_vdp2_types.h
};


/** Get vdp2 status */
typedef struct __attribute__((__packed__)) VDP2CMD_GetStatus
{   
    Cmd_Header_t header;        
    struct Res_Bus_Diag_Stats bus_stats;    // BusIO stats
} VDP2CMD_getStatus;


/** Get/Set vdp2 configuration */
typedef struct __attribute__((__packed__)) VDP2CMD_Config
{   
    Cmd_Header_t header;    
} VDP2CMD_Config;


/** Tile frame buffer */
typedef struct __attribute__((__packed__)) VDP2CMD_TileFrameBuffer16bpp
{   
    Cmd_Header_t header;    
    uint32_t cmdSeqNum;         // cmd seq num
    uint32_t cmdFlags;          // Command flag eg. flip display    
    uint16_t tileId;            // tile id    
    uint16_t pixels[FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE]; // scanline row data ( max 320x240 @ 16bpp )   
    uint8_t attr[FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE]; // scanline alpha data
    uint32_t passId;            // Padd index
    uint8_t defaultBlendMode;   // Zero pipeline default comp blend
    
    // Cmd data to exec        
    uint16_t vdp2CmdDataCount;          // Command cnt (cmdData CmdList total cmds)    
    uint32_t vdp2CmdDataSz;             // Size of cmd data ( malloc cmd sizeof(VDP1CMD_DrawCmdData) + cmdDataSz )               
    uint8_t vdp2CmdData[VDP2_TILE_CMD_DATA_SZ];  // Cmd data
} VDP2CMD_TileFrameBuffer16bpp;


/** Init draw command data size from cmd list ( variable sized cmds with offsets applied )
    VDP1CMD_DrawCmdData cmdOut  - target command to send
    GpuCommandList cmdList      - source command list     
*/
#define INIT_VDP2CMD_TileFrameBuffer_cmdlist_sz(cmdOut, cmdList) \
    BUS_INIT_CMD_PTR(cmdOut, EBusCmd_VDP2CMD_TileFrameBuffer16bpp); \
    cmdOut->header.sz = cmdList->offset; \
    cmdOut->cmdDataCount = cmdList->cmdCount; \
    cmdOut->cmdDataSz = cmdList->offset - cmdList->headerSz; \


/** Tile frame buffer 8bpp */
typedef struct __attribute__((__packed__)) VDP2CMD_TileFrameBuffer8bpp
{   
    Cmd_Header_t header;    
    uint32_t cmdSeqNum;         // cmd seq num
    uint32_t cmdFlags;          // Command flag eg. flip display    
    uint16_t tileId;            // tile id    
    uint8_t pixels[FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE]; // scanline row data ( max 320x240 @ 16bpp )       
    uint32_t passId;            // Padd index
    uint8_t defaultBlendMode;   // Zero pipeline default comp blend
    uint16_t globalVdp2PalBufferId;

    // Cmd data to exec        
    uint16_t vdp2CmdDataCount;          // Command cnt (cmdData CmdList total cmds)    
    uint32_t vdp2CmdDataSz;             // Size of cmd data ( malloc cmd sizeof(VDP1CMD_DrawCmdData) + cmdDataSz )               
    uint8_t vdp2CmdData[VDP2_TILE_CMD_8BPP_DATA_SZ];  // Cmd data
} VDP2CMD_TileFrameBuffer8bpp;


/** Init draw command data size from cmd list ( variable sized cmds with offsets applied )
    VDP1CMD_DrawCmdData cmdOut  - target command to send
    GpuCommandList cmdList      - source command list     
*/
#define INIT_VDP2CMD_TileFrameBuffer8bpp_cmdlist_sz(cmdOut, cmdList) \
    BUS_INIT_CMD_PTR(cmdOut, EBusCmd_VDP2CMD_TileFrameBuffer8bpp); \
    cmdOut->header.sz = cmdList->offset; \
    cmdOut->cmdDataCount = cmdList->cmdCount; \
    cmdOut->cmdDataSz = cmdList->offset - cmdList->headerSz; \



/** Background draw commands to composite with scanlines, must be submitted before effected scanline */
typedef struct __attribute__((__packed__)) VDP2CMD_BackgroundDrawCmdList
{       
    Cmd_Header_t header;    
    uint32_t frameId;       // Link frame ID to scanline compositor, discarded if older than current frame
    uint16_t drawCmdCnt;    // drawCommands[] count
    VDP1CMD_DrawCmdData* drawCommands; 
} VDP2CMD_BackgroundDrawCmdList;
