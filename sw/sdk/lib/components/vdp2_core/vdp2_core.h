#pragma once

#include "picocom/platform.h"
#include "picocom/display/display.h"
#include "gpu/gpu.h"
#include "platform/pico/bus/bus.h"
#include "lib/platform/pico/vdp2/hw_vdp2_types.h"

/** vdp2 init options */
typedef struct vdp2InitOptions_t
{
    BusTx_t* vdp1_xlnk_tx;
    BusRx_t* vdp1_vdbus_rx;      
    struct GpuInitOptions_t gpuOptions;
    bool initDisplayDriver;
} vdp2Options_t;


/** vdp2 run options */
typedef struct vdp2MainLoopOptions_t
{
} vdp2MainLoopOptions_t;


/** VDP 1 state */
typedef struct vdp2_t
{
    BusTx_t* vdp1_xlnk_tx;                                // (out) VDP2 -> VDP1,      narrow 1 bit bus
    BusRx_t* vdp1_vdbus_rx;                               // (in) VDP1 -> VDP2,       wide 8 bit bus
    struct GpuState_t* gpuState;                          // Gpu state   
    struct GpuInstance_t gpuInstances[1];                 // Per core gpu instance
    struct Cmd_Header_t* tileCmd;                         // Pending tile to comp    
    uint32_t drawCmdMaxSz;                              // Max alloc size (should match rx bus max packet)    
    struct Cmd_Header_t* pendingTileCmd;                  // Pending tile to comp     
    uint32_t flipCount;
    uint32_t startupTime;
} vdp2_t;

// vdp2 api
int vdp2_init(struct vdp2_t* vdp, struct vdp2InitOptions_t* options);
int vdp2_deinit(struct vdp2_t* vdp);
int vdp2_main(struct vdp2_t* vdp, struct vdp2MainLoopOptions_t* options);
int vdp2_update(struct vdp2_t* vdp, struct vdp2MainLoopOptions_t* options);
void vdp_render_error(struct vdp2_t* vdp, uint32_t errorCode, const char* msg);