#pragma once

#include "gpu_types.h"
#include <stdint.h>
#ifdef PICOCOM_SDL
#include "lib/components/mock_hardware/mutex.h"
#else
#include <pico/mutex.h>
#endif
#include "components/flash_store/flash_store.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fwd
struct GpuState_t;
struct GpuInstance_t;
struct GpuValidationOutput_t;

// shader exec
typedef void (*ShaderCmdExec_t)(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* cmd, TileFrameBuffer_t* tile);
typedef bool (*ShaderCmdValidate_t)(const struct GpuState_t* gpu, struct GpuCmd_Header* cmd, struct GpuValidationOutput_t* info);
typedef bool (*ShaderCmdToString_t)(const struct GpuState_t* gpu, struct GpuCmd_Header* cmd, char* buff, uint32_t buffSz);


/** Gpu validate macro assert
*/
#ifdef GPU_DEBUG
#define GPU_VALIDATE_ASSERT(Expression) \
if( (!(Expression)) ) { \
    gpu_validate_assert((#Expression), (__FILE__), __LINE__, header, info); \
    return false; \
}\

#else
#define GPU_VALIDATE_ASSERT(Expression) 
#endif

/** Gpu init options */
typedef struct GpuInitOptions_t
{
    uint32_t bufferRamSz;
    bool enableFlash;
} GpuInitOptions_t;


/** Gpu cmd info */
typedef struct GpuCmdInfo_t
{
    const char* name;
    uint8_t id;
    ShaderCmdExec_t cmd;
    ShaderCmdValidate_t validator;
    ShaderCmdToString_t toString;
} GpuCmdInfo_t;


/* Gpu validator output */
typedef struct GpuValidationOutput_t {
    uint32_t errorCode;
    uint32_t listIndex;
    uint32_t cmdIndex;
    uint32_t cmdDataOffset;
    const char* errorMessage;
    const char* errorSource;    
    int errorLine;
    int headerSz;           // Raw packet header size        
    int expectedSize;       // Check expected size
    int extraArg0;          // Per debug arg eg. buffer id
    int extraCmdBaseSize;   // Base size of cmd
    int extraDataBaseSize;  // Size of data elements (variable sized cmds)
    //uint8_t buffersCreated[GPU_MAX_BUFFER_ID];
} GpuValidationOutput_t;


/** Result of gpu run */
typedef struct GpuResult_t
{

} GpuResult_t;


/** Gpu state */
typedef struct GpuState_t
{    
    const char* debugName;
    
    struct GpuInitOptions_t options;

    // commands
    GpuCmdInfo_t cmdInfos[GPU_MAX_SHADER_CMD_ID];    
    ShaderCmdExec_t cmds[GPU_MAX_SHADER_CMD_ID];    
    
    // Buffers
    struct GpuBufferInfo buffers[GPU_MAX_BUFFER_ID];
    mutex_t bufferWriteLock;

    // Ram arena
    uint8_t* ram0BufferBase;            // Ram0 base
    uint32_t ram0BufferSz;              // Ram0 size

    // Flash
    struct FlashStore_t* flashStore;    // Flash storage  

    // Jit methods
    void* jitMethods[0xff];             // exported methods into jitted cmds (eg. calling sqrt from shader code, must call method table due to code relocation)

    void* globalState3D[2];              // 3D extention state 
} GpuState_t;



/** Gpu job state for a shared gpu state */
typedef struct GpuInstance_t
{
    uint32_t instanceId;

    // current stats
    bool inFrame;
    uint32_t frameStartTime;
    uint32_t frameTileCnt;
    uint32_t tileMaxTime;

    GpuFrameStats_t frameStats;
    GpuFrameProfile frameProfile;
    bool enableProfiler;
    bool debugCommandUploads;

    // render state    
    struct TileFrameBuffer_t* fb0;    // bound composite tile

    // debug
    bool enableDebuger;
    int debugCmdId;
    int debugSelectCmdIndex;
  
} GpuInstance_t;


// gpu api
uint8_t* gpu_malloc(uint32_t sz);                                               // gpu related malloc
GpuState_t* gpu_init(struct GpuInitOptions_t* options);                         // Init gpu
void gpu_init_instance(GpuState_t* state, GpuInstance_t* job, uint32_t instanceId);  // Init gpu instance eg. per core
void gpu_deinit(GpuState_t* gpu);                                                // De-init gpu
void gpu_reset(GpuState_t* state, uint8_t cmds, uint8_t buffers, uint8_t stats); // Reset gpu state
void gpu_register_cmd(GpuState_t* state, GpuCmdInfo_t info);                    // Register gpu cmd
void gpu_init_commands(GpuState_t* state);                                      // Register default gpu commands
void gpu_begin_frame(GpuState_t* state, GpuInstance_t* job, uint32_t frameId); 
void gpu_end_frame(GpuState_t* state, GpuInstance_t* job); 
void gpu_clear_error_stats(GpuState_t* state, GpuInstance_t* job);              // prepare tile run, clear error counters
void gpu_run_tile(GpuState_t* state, GpuInstance_t* job, GpuCommandList_t* commands, TileFrameBuffer_t* tile);    // Render command into tile
void gpu_dump_cmds(GpuState_t* state, GpuCommandList_t* commands);
bool gpu_validate_cmds_list(GpuState_t* state, GpuCommandList_t* list, struct GpuValidationOutput_t* info);  // Check commands valid and output crc, returns true if validation passed
bool gpu_validate_cmds_listlist(GpuState_t* state, GpuCommandListList_t* listlist, struct GpuValidationOutput_t* info);  // Check commands valid and output crc, returns true if validation passed
int gpu_register_cmd_jit(GpuState_t* state, uint8_t cmdId, uint8_t bufferId);   // Register gpu buffer as cmd jit code
uint16_t gpu_calc_tile_cull_mask(int32_t y, int32_t h);                         // Calc tile mask for cmd y pos and height
uint16_t gpu_calc_tile_cull_mask_line(int32_t y0, int32_t y1);                  // Calc tile mask coverage over line, any order of y allowed
void gpu_diag_buffers(GpuState_t* state);                                       // dump buffer diags
struct GpuBufferInfo* gpu_get_buffer_by_id(GpuState_t* state, uint32_t id);
void gpu_debug_dump_state(GpuState_t* state, bool dumpBufferData);              // Dump entire gpu state to UART
void gpu_dump_buffer_state(GpuState_t* state, uint32_t bufferId);               // Dump buffer
void gpu_bind_fbo(GpuState_t* state, GpuInstance_t* job, struct TileFrameBuffer_t* fb0); // Bind tile frame buffer

// gpu utils
uint8_t* gpu_malloc(uint32_t sz);
void gpu_free(uint8_t* ptr);
struct Col16_t gpu_col_from_rgba_hex(uint32_t rgb);
struct Col16_t gpu_col_from_rgbf(float r, float g, float b);
uint16_t gpu_col_to_uint16(struct Col16_t col);
struct Col16_t gpu_col_from_rgb565(uint16_t r, uint16_t g, uint16_t b);
struct Col16_t gpu_col_from_uint16(uint16_t coli);
struct ColRGBF_t gpu_col_to_rgbf(struct Col16_t col);
struct ColRGBF_t gpu_coli_to_rgbf(uint16_t coli);
struct Col16_t gpu_rgbf_to_col(struct ColRGBF_t colf);
uint16_t gpu_col_add_uint16(uint16_t a, uint16_t b);
void gpu_print_validator_error(struct GpuValidationOutput_t* info);
void gpu_validate_assert(const char* message, const char* file, unsigned line, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info);
void gpu_error(struct GpuState_t* gpu, struct GpuInstance_t* job, int errorCode, const char* desc);

// simulated flash 
#ifdef PICOCOM_SDL
    int save_and_disable_interrupts();
    int restore_interrupts (int flags);
    void flash_range_erase (uint32_t flash_offs, size_t count);
    void flash_range_program (uint32_t flash_offs, const uint8_t *data, size_t count);
    void flash_simulator_set_xip_bases (uint8_t* xipBase, uint8_t* xipEnd);
#endif

#ifdef __cplusplus
}
#endif
