#include "picocom/devkit.h"
#include "picocom/display/display.h" // used for debug related
#include "thirdparty/crc16/crc.h"
#ifdef PICOCOM_PICO
    #include "pico/stdlib.h"
#else
    #define GPU_FLASH_BUFFER_PAGE_SIZE 4096
#endif
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "command_list.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
 #include <inttypes.h>
#include "platform/pico/vdp2/hw_vdp2_types.h" // DEP: VDP_TILEFRAME_BUFFER_Y_SIZE


//
// globals 
static int gpu_total_alloc = 0;


//
// flash simulator
#ifdef PICOCOM_SDL
static /*__thread*/ uint8_t *XIP_BASE = NULL;       // per thread flash
static /*__thread*/ uint8_t *XIP_END = NULL;        

int save_and_disable_interrupts()
{
    return 0;
}


int restore_interrupts (int flags)
{
    return flags;
}


void flash_range_erase (uint32_t flash_offs, size_t count)
{        
    if(!XIP_BASE || !XIP_END)
    {
        picocom_panic(SDKErr_Fail, "XIP_BASE & XIP_END not initialised for thread");
    }

    if(flash_offs+count > PICO_FLASH_SIZE_BYTES)
    {
        picocom_panic(SDKErr_Fail, "flash_offs > PICO_FLASH_SIZE_BYTES");
    }

    // page align check
    if(flash_offs % GPU_FLASH_BUFFER_PAGE_SIZE != 0)
    {
        picocom_panic(SDKErr_Fail, "Non page aligned flash program");
    }

    // copy
    uint8_t* dstPtr = XIP_BASE + flash_offs;

    if( dstPtr + count >= XIP_END)
    {
        int totalXip = XIP_END-XIP_BASE;
        picocom_panic(SDKErr_Fail, "flash page overflowed flash");
    }

    memset(dstPtr, 0, count);
}


void flash_range_program (uint32_t flash_offs, const uint8_t *data, size_t count)
{
    if(flash_offs+count > PICO_FLASH_SIZE_BYTES)
    {
        picocom_panic(SDKErr_Fail, "flash_offs > PICO_FLASH_SIZE_BYTES");
    }

    // page align check
    if(flash_offs % GPU_FLASH_BUFFER_PAGE_SIZE != 0)
    {
        picocom_panic(SDKErr_Fail, "Non page aligned flash program");
    }

    // copy
    uint8_t* dstPtr = XIP_BASE + flash_offs;

    if( dstPtr + count >= XIP_END)
    {
        picocom_panic(SDKErr_Fail, "flash page overflowed flash");
    }

    memcpy(dstPtr, data, count);
}


void flash_simulator_set_xip_bases (uint8_t* xipBase, uint8_t* xipEnd)
{
    XIP_BASE = xipBase;
    XIP_END = xipEnd;      
}

#endif


//
//
uint8_t* gpu_malloc(uint32_t sz)
{
    uint8_t* buffer = (uint8_t*)picocom_malloc(sz);
    if(!buffer)
        return 0;

    gpu_total_alloc += sz;
    
    return buffer;
}


void gpu_free(uint8_t* ptr)
{
    picocom_free(ptr);
}


//
//
GpuState_t* gpu_init( struct GpuInitOptions_t* options)
{
    GpuState_t* state = (GpuState_t*)gpu_malloc(sizeof(GpuState_t));
    if(!state)
        return 0;
    memset(state, 0, sizeof(GpuState_t));

    // Alloc ram0
    state->ram0BufferBase = gpu_malloc(options->bufferRamSz); // Gpu buffer arena
    if(!state->ram0BufferBase)
        picocom_panic(SDKErr_Fail, "gpu buffer arena alloc fail");
    state->ram0BufferSz = options->bufferRamSz;

    // init export
    state->jitMethods[EGpuJitMethod_sqrtf] = sqrtf;
    
    if( options->enableFlash )
    {
        state->flashStore = (struct FlashStore_t*)gpu_malloc(sizeof(struct FlashStore_t));
    }
    if( state->flashStore )
    {
        if( flash_store_init_xip( state->flashStore, 0, GPU_FLASH_STORAGE_SIZE ) != SDKErr_OK )
            picocom_panic( SDKErr_Fail, "flash_store_init_ram failed");
    }

    mutex_init(&state->bufferWriteLock);

    // Reset to default state, buffers & cmds
    gpu_reset(state, 1, 1, 1);
    
    return state;
}


void gpu_job_init( GpuState_t* state, GpuInstance_t* job)
{
    memset(job, 0, sizeof(*job));
    job->debugSelectCmdIndex= -1;
}


void gpu_deinit(GpuState_t* state)
{
    if(!state)
        return;
    
    if(state->ram0BufferBase)
        gpu_free((uint8_t*)state->ram0BufferBase);

    gpu_free((uint8_t*)state);
}


void gpu_clear_error_stats(GpuState_t* state, GpuInstance_t* job)
{
    job->frameStats.cmdErrors = 0;
}


void gpu_run_tile(GpuState_t* state, GpuInstance_t* job, GpuCommandList_t* commands, TileFrameBuffer_t* tile)
{    
    if(!commands)
        return;
        
    uint32_t startTime = picocom_time_us_32();
    uint16_t tileMask = 1 << tile->tileId;
    
    int i = commands->headerSz;   
    int index = 0;       
    uint32_t cmdStart;
    while(i < commands->allocSz && index < commands->cmdCount)
    {
        GpuCmd_Header* header = (GpuCmd_Header*)&commands->cmdData[i];    
        if(header->sz == 0)
            break;
        
        // cull tile        
        if( header->flags & EGpuCmd_Header_Flags_TileCullMask ) // zero/uninit no culling run on all
        {
            if((tileMask & header->cullTileMask) == 0)
            {
                // profiler
                if(job->enableProfiler)
                {            
                    job->frameProfile.frameCullCounts++;
                }        
                // next
                i += header->sz;
                index++;           
                continue;
            }
        }

        job->debugCmdId++;            

        // profiler
        if(job->enableProfiler)
        {
            //job->frameProfile.cmdExecCounts[header->cmd]++;
            cmdStart = picocom_time_us_32();
        }

        // check cull

        // dispatch
        if(header->cmd < GPU_MAX_SHADER_CMD_ID)
        {
            ShaderCmdExec_t cmd = state->cmds[header->cmd];
            if(cmd)
            {
                if(job->enableDebuger && job->debugSelectCmdIndex != -1)
                {
                    if(job->debugSelectCmdIndex == job->debugCmdId)
                        cmd(state, job, header, tile);
                }
                else
                {
                    cmd(state, job, header, tile);
                }
            }
        }

        if(header->sz > 65535)
        {            
            picocom_panic(SDKErr_Fail, "Corrupt GPU Cmd, header->sz > 65535");
        }

        i += header->sz;
        index++;
        
        // profiler
        if(job->enableProfiler)
        {            
            uint32_t took = picocom_time_us_32() - cmdStart;
            //if(took > job->frameProfile.cmdMaxTime[header->cmd])
              //  job->frameProfile.cmdMaxTime[header->cmd] = took;
        }        
    }

    // profile
    uint32_t took = picocom_time_us_32() - startTime;
    if(took > job->tileMaxTime)
        job->tileMaxTime = took;    
}


void gpu_dump_cmds(GpuState_t* state, GpuCommandList_t* commands)
{
    int i = commands->headerSz;        
    int totalCmds = 0;

    printf("\tcmds: %d\n", commands->cmdCount);
    int index = 0;
    while(i < commands->allocSz && index < commands->cmdCount)
    {
        GpuCmd_Header* header = (GpuCmd_Header*)&commands->cmdData[i];    
        if(header->sz == 0)
            break;
        totalCmds++;

        char cullMaskStr[FRAME_TILE_CNT_Y+1] = {};
        for(int j=0;j<FRAME_TILE_CNT_Y;j++)
            cullMaskStr[j] = (header->cullTileMask & (1 << j)) ? '*' : '-';        

        if(state && state->cmdInfos[header->cmd].toString)
        {
            char buff[128];
            const char* name = state->cmdInfos[header->cmd].name ? state->cmdInfos[header->cmd].name : "";
            if(state->cmdInfos[header->cmd].toString(state, header, buff, sizeof(buff)-1))
                buff[sizeof(buff)-1] = 0;
            else
                buff[0] = 0;
                    
            printf("\t\tCmd[%d] cmd:%d, sz:%d %s(%s), cull: 0x%x[%s]\n", index, header->cmd, header->sz, name, buff, header->cullTileMask, cullMaskStr);
        }
        else {
            switch (header->cmd) {
            default:
            {
                const char* name = "U";
                if( state && state->cmdInfos[header->cmd].name)
                    name =  state->cmdInfos[header->cmd].name;
                printf("\t\tCmd[%d] cmd:%d (%s), sz:%d, cull: 0x%x[%s]\n", index, header->cmd, name, header->sz, header->cullTileMask, cullMaskStr);
                break;
            }
            }
        }
        
        i += header->sz;
        index++;
    }  
    if(totalCmds != commands->cmdCount)
        printf("\tdiff cmd, counted: %d vs commands->cmdCount: %d\n", totalCmds, commands->cmdCount);
}


void gpu_begin_frame(GpuState_t* state, GpuInstance_t* job, uint32_t frameId)
{
    job->inFrame = 1;
    job->frameStartTime = picocom_time_us_32();
    job->frameStats.cmdSeqNum = frameId;

    // reset counters
    job->frameTileCnt = 0;
    job->tileMaxTime = 0;    
    if(job->enableProfiler)
    {
        memset(&job->frameProfile, 0, sizeof(job->frameProfile)); // zero
        job->frameProfile.cmdSeqNum = frameId;
    }
    job->fb0 = 0;
}


void gpu_bind_fbo(GpuState_t* state, GpuInstance_t* job, struct TileFrameBuffer_t* fb0)
{
    job->fb0 = fb0;
}


void gpu_end_frame(GpuState_t* state, GpuInstance_t* job)
{
    job->inFrame = 0;

    // calc frame time
    job->frameStats.isValid = true;
    job->frameStats.frameTime = picocom_time_us_32() - job->frameStartTime;    

    // tile stats
    job->frameStats.tileCnt = job->frameTileCnt;
    job->frameStats.tileMaxTime = job->tileMaxTime;

    if(job->enableProfiler)
    {
        job->frameProfile.isValid = true;
    }
    else 
    {
        job->frameProfile.isValid = false;
    }
}


bool gpu_validate_cmds_list(GpuState_t* state, GpuCommandList_t* commands, struct GpuValidationOutput_t* info)
{
    int i = commands->headerSz;   
    int index = 0;       
    uint32_t cmdStart;
    while(i < commands->allocSz && index < commands->cmdCount)
    {
        GpuCmd_Header* header = (GpuCmd_Header*)&commands->cmdData[i];    
        if(header->sz == 0)
            break;

        // dispatch
        if(header->cmd < GPU_MAX_SHADER_CMD_ID)
        {
            ShaderCmdValidate_t cmd = state->cmdInfos[header->cmd].validator;
            if(cmd)
            {
                if(!cmd(state, header, info))
                {
                    info->cmdIndex = index;
                    info->cmdDataOffset = i;
                    return false;
                }
            }
        }
    
        i += header->sz;
        index++;       
    }
 
    return true;
}


bool gpu_validate_cmds_listlist(GpuState_t* state, GpuCommandListList_t* cmdListList, GpuValidationOutput_t* info)
{
    if(!info)
        return false;
    memset(info, 0, sizeof(GpuValidationOutput_t));

    int listCnt = gpu_cmd_list_list_get_count(cmdListList);
    for(int i=0;i<listCnt;i++)
    {
        struct GpuCommandList_t* cmdList = cmdListList->lists[i];
        if(!gpu_validate_cmds_list(state, cmdList, info))
        {
            info->listIndex = i;
            return false;
        }
    }
    return true;
}


void gpu_print_validator_error( struct GpuValidationOutput_t* info)
{
    printf("errorCode: %d\n", info->errorCode);
    printf("errorLine: %d\n", info->errorLine);
    printf("errorMessage: %s\n", info->errorMessage ? info->errorMessage : "");
    printf("errorSource: %s\n", info->errorSource ? info->errorSource : "");
    printf("cmdDataOffset: %d\n", info->cmdDataOffset);
    printf("cmdIndex: %d\n", info->cmdIndex);    
    printf("listIndex: %d\n", info->listIndex);    
}


void gpu_register_cmd(GpuState_t* state, GpuCmdInfo_t info)
{
    state->cmdInfos[info.id] = info;
    state->cmds[info.id] = info.cmd;
}


int gpu_register_cmd_jit(GpuState_t* state, uint8_t cmdId, uint8_t bufferId)
{
    if(bufferId >= GPU_MAX_BUFFER_ID)
    {
        return SDKErr_Fail;
    }

    struct GpuBufferInfo* buffer = &state->buffers[bufferId];
    if(!buffer->isValid)
    {
        return SDKErr_Fail;
    }

    if(!buffer->basePtr)
    {
        return SDKErr_Fail;
    }

    struct GpuCmdInfo_t info;
    memset(&info, 0, sizeof(info));
    info.id = cmdId;
#ifdef PICOCOM_SDL
    // Use void ptr
    ShaderCmdExec_t* fun = (ShaderCmdExec_t*)buffer->basePtr;
    info.cmd = *fun; // copy fun ptr
#else    
    info.cmd = (ShaderCmdExec_t)(buffer->basePtr + 1); // Pico arm opcodes
#endif

    state->cmdInfos[info.id] = info;
    state->cmds[info.id] = info.cmd;

    return SDKErr_OK;
}


struct Col16_t gpu_col_from_rgbf(float rf, float gf, float bf)
{
    rf = MIN(rf,1.0f);
    gf = MIN(gf,1.0f);
    bf = MIN(bf,1.0f);
    rf = MAX(rf,0.0f);
    gf = MAX(gf,0.0f);
    bf = MAX(bf,0.0f);

    float r = (float)rf * 31;
    float g = (float)gf * 63;
    float b = (float)bf * 31;

    return (struct Col16_t){
        .b = (uint16_t)b,
        .g = (uint16_t)g,
        .r = (uint16_t)r
    };
}

struct Col16_t gpu_col_from_rgba_hex(uint32_t rgb)
{
    return gpu_col_from_rgbf(
        ((rgb & 0xff0000) >> 16) / 255.0f,
        ((rgb & 0xff00) >> 8) / 255.0f,
        ((rgb & 0xff) >> 0) / 255.0f
    );
}


uint16_t gpu_col_to_uint16(struct Col16_t col)
{
    return col.value;
}


struct Col16_t gpu_col_from_rgb565(uint16_t r, uint16_t g, uint16_t b)
{
    return (struct Col16_t){
        .b = b,
        .g = g,
        .r = r
    };
}


struct Col16_t gpu_col_from_uint16(uint16_t coli)
{
    struct Col16_t r;
    r.value = coli;
    return r;
}


struct ColRGBF_t gpu_col_to_rgbf(struct Col16_t col)
{
    return (struct ColRGBF_t){    
        .r = (float)col.r / 31,
        .g = (float)col.g / 63,
        .b = (float)col.b / 31
    };
}


struct ColRGBF_t gpu_coli_to_rgbf(uint16_t coli)
{
    struct Col16_t r;
    r.value = coli;
    return gpu_col_to_rgbf(r);
}


struct Col16_t gpu_rgbf_to_col(struct ColRGBF_t colf)
{
    return gpu_col_from_rgbf(colf.r, colf.g, colf.b);
}


uint16_t gpu_col_add_uint16(uint16_t a, uint16_t b)
{
    Col16_t ca, cb, res;
    res.value = 0xffff;
    ca.value = a;
    cb.value = b;

    if(ca.r + cb.r < 31)
        res.r = ca.r + cb.r;
    if(ca.g + cb.g < 63)
        res.g = ca.g + cb.g;        
    if(ca.b + cb.b < 31)
        res.b = ca.b + cb.b;
    return res.value;        
}


void gpu_reset(GpuState_t* state, uint8_t cmds, uint8_t buffers, uint8_t stats)
{
    if(!state)
        return;

    // Buffers
    if(buffers)
    {
        memset(&state->buffers, 0, sizeof(state->buffers));

        // Ram arena
        if( state->ram0BufferBase && state->ram0BufferSz)
            memset(state->ram0BufferBase,0, state->ram0BufferSz ); 
    }

    // commands
    if(cmds)
    {
        memset(&state->cmdInfos, 0, sizeof(state->cmdInfos));
        memset(&state->cmds, 0, sizeof(state->cmds));        
        // init default commands 
        gpu_init_commands(state);        
    }    
}


void gpu_init_instance(GpuState_t* state, GpuInstance_t* job, uint32_t instanceId)
{
    memset(job, 0, sizeof(*job));
    job->instanceId = instanceId;
}


uint16_t gpu_calc_tile_cull_mask(int32_t y, int32_t h)
{
    int32_t minY = y >= 0 ? y : 0;
    if(minY > FRAME_H)
        return 0; // out of frame

    int32_t maxY = y+h < FRAME_H ? y+h : FRAME_H;
    if(maxY < 0)
        return 0; // out of frame

    uint32_t startY = FLOOR_INT(minY, VDP_TILEFRAME_BUFFER_Y_SIZE);
    uint32_t endY = CEIL_INT(maxY, VDP_TILEFRAME_BUFFER_Y_SIZE);
    uint16_t mask = 0;
    for(int i=startY;i<endY;i++)
        mask |= (1 << (i));
    return mask;  // mask + masked flag
}


uint16_t gpu_calc_tile_cull_mask_line(int32_t minY, int32_t maxY)
{
    if(minY > FRAME_H)
        return 0; // out of frame
    if(maxY < 0)
        return 0; // out of frame

    uint32_t startY = FLOOR_INT(minY, VDP_TILEFRAME_BUFFER_Y_SIZE);
    uint32_t endY = CEIL_INT((maxY), VDP_TILEFRAME_BUFFER_Y_SIZE);
    uint16_t mask = 0;
    for(int i=startY;i<endY;i++)
        mask |= (1 << (i));
    return mask;
}


struct GpuBufferInfo* gpu_get_buffer_by_id(GpuState_t* state, uint32_t id)
{
    if(id >= GPU_MAX_BUFFER_ID)
        return 0;

    struct GpuBufferInfo* buffer = &state->buffers[id];
    if(!buffer->isValid)
        return 0;

    if(!buffer->basePtr)
        return 0;

    return buffer;
}


void gpu_debug_dump_job(GpuState_t* state, GpuInstance_t* job)
{
    printf("inFrame: %d\n", job->inFrame );
    printf("frameStartTime: %d\n", job->frameStartTime );
    printf("frameTileCnt: %d\n", job->frameTileCnt );
    printf("tileMaxTime: %d\n", job->tileMaxTime );        
    printf("enableProfiler: %d\n", job->enableProfiler );    
    printf("debugCommandUploads: %d\n", job->debugCommandUploads );        
    printf("fb0: 0x%" PRIXPTR "\n", (uintptr_t)job->fb0 );            
    printf("debugCmdId: %d\n", job->debugCmdId );
    printf("debugSelectCmdIndex: %d\n", job->debugSelectCmdIndex );
}


void gpu_debug_dump_state(GpuState_t* state, bool dumpBufferData)
{
    printf("debugName: %s\n", state->debugName ? state->debugName : "NULL" );
    printf("options.bufferRamSz: %d\n", state->options.bufferRamSz );    
    printf("ram0BufferBase: 0x%"PRIXPTR"\n", (uintptr_t)state->ram0BufferBase );
    printf("ram0BufferSz: %d\n", state->ram0BufferSz );
    
    int validBufferCnt = 0;
    for(int i=0;i<GPU_MAX_BUFFER_ID;i++)
    {
        struct GpuBufferInfo* buffer = &state->buffers[i];
        if(!buffer->isValid)
            validBufferCnt++;
    }
    printf("validBufferCount: %d\n", validBufferCnt );

    for(int i=0;i<GPU_MAX_BUFFER_ID;i++)
    {
        struct GpuBufferInfo* buffer = &state->buffers[i];
        if(!buffer->isValid)
        {
            //printf("buffer[%d] !isValid\n", i);
            continue;
        }

        printf("buffer[%d]\n", i);
        printf("\tbasePtr: 0x%" PRIXPTR "\n", (uintptr_t)buffer->basePtr );
        printf("\tarenaId: %d\n", buffer->arenaId );
        printf("\tarenaOffset: %d\n", buffer->arenaOffset );
        printf("\tsize: %d\n", buffer->size );
        printf("\tlocked: %d\n", buffer->locked );
        printf("\twriteCnt: %d, finalWriteCnt: %d\n", buffer->writeCnt, buffer->finalWriteCnt );
        printf("\ttextureFormat: %d\n", buffer->textureFormat );
        printf("\tw: %d\n", buffer->w );
        printf("\th: %d\n", buffer->h );
        for(int j=0;j<9;j++)
        {
            printf("\t\tedgeData[%d] 0x%"PRIXPTR"\n", j, (uintptr_t)buffer->edgeData[j] );
        }
        printf("\tcrc: 0x%X\n", buffer->basePtr ? picocom_crc16(buffer->basePtr, buffer->size) : 0 );

        if(dumpBufferData)
        {
            printf("\t\t");
            for(int i=0;i<buffer->size;i++)
            {
                printf("%02X ", buffer->basePtr[i]);
            }
            printf("\n");
        }
    }
}


void gpu_dump_buffer_state(GpuState_t* state, uint32_t bufferId)
{
    if(bufferId >= GPU_MAX_BUFFER_ID)
        return;

    struct GpuBufferInfo* buffer = &state->buffers[bufferId];
    if(!buffer->isValid)
    {
        printf("buffer[%d] !isValid\n", bufferId);
        return;
    }

    printf("buffer[%d]\n", bufferId);
    printf("\tbasePtr: 0x%" PRIXPTR "\n", (uintptr_t)buffer->basePtr );
    printf("\tarenaId: %d\n", buffer->arenaId );
    printf("\tarenaOffset: %d\n", buffer->arenaOffset );
    printf("\tsize: %d\n", buffer->size );
    printf("\tlocked: %d\n", buffer->locked );
    printf("\twriteCnt: %d, finalWriteCnt: %d\n", buffer->writeCnt, buffer->finalWriteCnt );
    printf("\ttextureFormat: %d\n", buffer->textureFormat );
    printf("\tw: %d\n", buffer->w );
    printf("\th: %d\n", buffer->h );
    for(int j=0;j<9;j++)
    {
        printf("\t\tedgeData[%d] 0x%"PRIXPTR"\n", j, (uintptr_t)buffer->edgeData[j] );
    }
    printf("\tcrc: 0x%X\n", buffer->basePtr ? picocom_crc16(buffer->basePtr, buffer->size) : 0 );

    printf("\t\t");
    for(int i=0;i<buffer->size;i++)
    {
        printf("%02X ", buffer->basePtr[i]);
    }
    printf("\n");
}
