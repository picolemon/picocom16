#pragma once

#include "picocom/platform.h"
#include "picocom/display/display.h"
#include "gpu/gpu.h"
#include "platform/pico/bus/bus.h"
#include "platform/pico/vdp1/hw_vdp1_types.h"
#include "platform/pico/vdp2/hw_vdp2_types.h"
#ifdef PICOCOM_SDL
#include "lib/components/mock_hardware/mutex.h"
#else
#include <pico/mutex.h>
#endif


// Config
#ifndef PICOCOM_NATIVE_SIM
    #define VDP1_MULTICORE_RENDER               // Multi core dispatch VDP tile renderer
#endif    


/** VDP1 init options */
typedef struct vdp1InitOptions_t
{
    BusTx_t* vdp2_vdbus_tx;
    BusRx_t* vdp2_xlnk_rx;    
    BusTx_t* app_vlnk_tx;
    BusRx_t* app_vlnk_rx;          
    uint32_t vdp2CmdFwdWriteBufferSz;       // Should match app max vdp2 command packet size
    struct GpuInitOptions_t gpuOptions;
} vdp1Options_t;


/** VDP1 run options */
typedef struct vdp1MainLoopOptions_t
{
} vdp1MainLoopOptions_t;


/** Tile list job */
typedef struct tileListJob_t
{
    uint32_t tileId;    
    uint32_t subTileId;    
    const struct VDP1CMD_DrawCmdData* cmdIn;        // copy of draw cmd    

    struct TileFrameBuffer_t tileFrameBuffer;         // gpu target buffer

    struct Cmd_Header_t* tileCmdOut;             // output vdp2 command with buffer   
    
    uint32_t cmdErrors;
    uint32_t tileRenderTookTime;
    uint32_t tileBusCopyTookTime;
} tileListJob_t;


/** Queue entry */
typedef struct
{
    struct tileListJob_t* job;
} queue_entry_t;


/** VDP 1 state */
typedef struct vdp1_t
{
    BusTx_t* vdp2_vdbus_tx;       // VDP1 -> VDP2 (out) wide 8bit tile writer bus
    BusRx_t* vdp2_xlnk_rx;        // VDP2 -> VDP1 (in) narrow 1bit status bus

    BusTx_t* app_vlnk_tx;         // VDP1 -> APP (out) app status bus
    BusRx_t* app_vlnk_rx;         // APP -> VDP1 (in) cmd bus & gpu upload
    
    enum EVDP1State state;
    struct GpuState_t* gpuState;                  // Gpu shared state
        
    uint32_t vdp2CmdFwdWriteBufferSz;
    uint8_t* vdp2CmdFwdWriteBuffer;             // app->vdp2 forwarding buffer

    mutex_t sendLock;
    queue_t jobQueue;
    queue_t completeQueue;

    struct tileListJob_t job0;
    struct tileListJob_t job1;

    struct Cmd_Header_t* tileCmdOut[2];      // output vdp2 command with buffer   
    uint32_t currentTileCmdId;
} vdp1_t;


// exports
extern struct vdp1_t* g_vdp1;

// vdp1 api
int vdp1_init(struct vdp1_t* vdp, struct vdp1InitOptions_t* options);
int vdp1_deinit(struct vdp1_t* vdp);
int vdp1_main(struct vdp1_t* vdp, struct vdp1MainLoopOptions_t* options);
int vdp1_update(struct vdp1_t* vdp, struct vdp1MainLoopOptions_t* options);
void vdp1_set_state(struct vdp1_t* vdp, enum EVDP1State state);
