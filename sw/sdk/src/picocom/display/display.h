#pragma once

#include <stdint.h>
#include "gpu/gpu_types.h"
#include "gpu/command_list.h"
#include "lib/platform/pico/bus/bus.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display config
#define FRAME_W 320									// Fixed screen size
#define FRAME_H 240
#define FRAME_TILE_SZ_Y 48                          // Tile rendering block size (render & sync block size)
#define FRAME_TILE_CNT_Y (FRAME_H/FRAME_TILE_SZ_Y)  // Tile count split based on VDP2CMD_TileFrameBuffer transfer size (determins render size)
#define DISPLAY_GPU_CMD_BUS_MAX_PACKET_SZ   1024*8    // max size of command list packet
#define DISPLAY_GPU_CMD_ALLOC_SZ   			1024*8	// cmd buffer allocation size
#define VDP1_GPU_RAM_SZ   					1024*64	// gpu buffer ram allocated to VDP1
#define VDP2_GPU_RAM_SZ   					1024*64	// gpu buffer ram allocated to VDP2
#define Display_Impl_State_MaxFrameTimeSamples 8	// FPS sampler
#define GPU_FLASH_STORAGE_SIZE 				(1024*1024*3.5) // Gpu flash buffer size ( 512k reserved for vdp progmem )

// Fwd
struct VdpClientImpl_t;


/** Color depth */
enum EColorDepth
{
	EColorDepth_None,
	EColorDepth_8BPP,
	EColorDepth_BGR565
};


/** Display options */
typedef struct DisplayOptions_t
{
	uint32_t maxPacketSize;						// Max packet size on VDP1 rx receiver.
	uint32_t cmdListAllocSize;					// Total size of cmd buffer per submission ( must be < bus rx size on vdp )		
} DisplayOptions_t;


/** Display fps stats */
typedef struct DisplayStats_t
{
	// local client frame stats
	uint32_t lastFrameTime;						// Last frame render time
	uint32_t minFrameTime;					
	uint32_t maxFrameTime;					 
	float avgFps;								// Avg frame rate
	uint32_t frameId;							// Last frame id

	// cmd submit pipeline
	uint32_t lastCmdSubmitSz;					// Last total cmd submit size
	uint32_t lastCmdSubmitPacketCnt;			// Last cmd packet count
	uint32_t lastCmdSubmitCnt;					// Last gpu cmd cnt

	// vdp stats
	GpuFrameStats_t gpuFrameStats[2];				// VDP1[0] & VDP2[1] frame stats
	GpuFrameProfile gpuFrameProfile[2];			// VDP1[0] & VDP2[1] detailed frame profile (if enabled)
} DisplayStats_t;


/** Display state */
typedef struct DisplayState_t
{	
	struct BusTx_t vdp1Link_tx;		// vdp bus link
    struct BusRx_t vdp1Link_rx;
	struct VdpClientImpl_t* client;	// vdp client instance
	struct DisplayStats_t stats;
	struct GpuState_t* gpuState;      // optional gpu state for debug trace	
	uint32_t frameStartTime;
	uint32_t frameTimeIndex;
	uint32_t frameTimes[Display_Impl_State_MaxFrameTimeSamples];     // fps sampler
} DisplayState_t;


/** Declare shader function generator
*   - creates shader methods
*       shader_get_size_MyShader()
*       shader_upload_MyShader(...)
*/
#define SHADER_FUN_PICO(shaderName) \
    extern char __start_for_##shaderName[]; \
    extern char __stop_for_##shaderName[]; \
    int shader_get_size_##shaderName() { \
        return (__stop_for_##shaderName - __start_for_##shaderName); \
    }\
    int shader_upload_##shaderName(uint8_t cmd, uint8_t bufferId, uint8_t groupId,  uint8_t arena, uint32_t memOffset) { \
        int codeSz = shader_get_size_##shaderName(); \
        return display_upload_shader_impl( cmd, bufferId, groupId,  (uint8_t *)__start_for_##shaderName, codeSz, arena, memOffset, 0,0,0); \
    }\
    void __attribute__((noinline, section("for_" #shaderName ))) shaderName
    
	
/** Declare shader function generator
*   - creates shader methods
*       shader_get_size_MyShader()
*       shader_upload_MyShader(...)
*/
#define SHADER_FUN_SDL(shaderName) \
    extern char __start_for_##shaderName[]; \
    extern char __stop_for_##shaderName[]; \
    void shaderName(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile); \
    int shader_get_size_##shaderName() { \
        return (__stop_for_##shaderName - __start_for_##shaderName); \
    }\
    int shader_upload_##shaderName(uint8_t cmd, uint8_t bufferId, uint8_t groupId,  uint8_t arena, uint32_t memOffset) { \
        ShaderCmdExec_t fun = shaderName; \
		return display_upload_shader_impl( cmd, bufferId, groupId, (uint8_t *)&fun, sizeof(fun), arena, memOffset, 0,0,0); \
    }\
    void __attribute__((noinline, section("for_" #shaderName ))) shaderName


// Misc debug
extern uint16_t debugColors[3];

/** Declare shader function for platform
*/
#ifdef PICOCOM_SDL
    #define SHADER_FUN SHADER_FUN_SDL
#else
    #define SHADER_FUN SHADER_FUN_PICO
#endif

// framebuffer driver ( vdp2->hdmi/tft framebuffer )
bool display_driver_init(); 					// init display
void display_driver_deinit(); 					// deinit display
uint16_t* get_display_buffer(); 				// get working back buffer
void display_buffer_copy_front(); 				// copy current back buffer to front
uint16_t* flip_display_blocking(); 				// flip display & wait for next back buffer

// Display api
struct DisplayOptions_t display_default_options();// Get display default options
int display_init();                     		// Init display api default
int display_init_with_options(struct DisplayOptions_t* options); // Init display api
int display_update();                   		// Tick display system, dispatch commands to vdps etc
int display_reset_gpu();						// Reset gpu hw
struct DisplayState_t* display_get_state();		// Get display state
struct VdpClientImpl_t* display_get_impl();		// VDP Client implementation
int display_begin_frame();						// Mark frame begin 
int display_end_frame();						// Mark frame end
void display_debug_dump_vdp_state();			// debug helper to dump entire vdp system to the uart

// low level gpu command api
DisplayStats_t* display_stats();					// get frame stats
float display_calc_avg_fps(DisplayStats_t* stats);	// Calc fps
void display_print_stats();							// Print detailed display stats
int display_set_profiler_enabled(bool enabled, uint32_t level); // Enable profiler settings, profiler level > 1 sends full gpu profile stats (which will probably kill fps)
int display_upload_buffer(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena, uint32_t memOffset, uint8_t textureFormat, uint16_t w, uint16_t h); // upload and create buffer to gpu (blocking)
int display_upload_buffer_compressed(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint32_t compressedSize, uint8_t arena, uint32_t memOffset, uint8_t textureFormat, uint16_t w, uint16_t h); // upload zlib compressed and create buffer to gpu (blocking)
int display_create_buffer(uint8_t bufferId, uint8_t vdpId, uint32_t sz, uint8_t arena, uint32_t memOffset, uint8_t textureFormat, uint16_t w, uint16_t h, uint16_t parentBufferId); // create buffer to gpu (blocking)
int display_update_buffer_data(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena); // upload buffer
int display_update_buffer_data_ex(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena, uint32_t offsetInBuffer); // upload buffer
int display_update_buffer_data_compressed(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena, uint32_t memOffset); // upload compressed buffer
int display_register_buffer_cmd(uint8_t bufferId, uint8_t vdpId, uint8_t cmdId);	 // Register buffer as gpu cmd, allows custom shader code to run on vdp1/2
int display_upload_shader_impl(uint8_t cmd, uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena, uint32_t memOffset, uint8_t textureFormat, uint16_t w, uint16_t h); // internal shader macro helper

#ifdef __cplusplus
}
#endif