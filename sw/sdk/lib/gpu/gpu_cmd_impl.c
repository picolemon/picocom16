#include "picocom/devkit.h"
#include "gpu_types.h"
#include <stdint.h>
#include "gpu.h"
#include "thirdparty/crc16/crc.h"
#include "command_list.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include "picocom/utils/random.h"
#include "platform/pico/vdp2/hw_vdp2_types.h"
#include "picocom/utils/profiler.h"


//
// Globals
int g_profileCounter0 = 0;
bool g_test_set_clocks = false;


//
// helpers
int modi(int a, int b)
{
    int res = a % b;
    return res < 0 ? res + b : res;
}

// CREDIT: https://stackoverflow.com/questions/18937701/combining-two-16-bits-rgb-colors-with-alpha-blending
static inline uint16_t ALPHA_BLIT16_565(uint32_t fg, uint32_t bg, uint8_t alpha) {
    // Alpha converted from [0..255] to [0..31]
    uint32_t ALPHA = alpha >> 3;     
    fg = (fg | fg << 16) & 0x07e0f81f;
    bg = (bg | bg << 16) & 0x07e0f81f;
    bg += (fg - bg) * ALPHA >> 5;
    bg &= 0x07e0f81f;
    return (uint16_t)(bg | bg >> 16);
}


// 
// tilemaps
static int tileLookup[256];
void initTilemap() {
	tileLookup[0] = 47; // blank
	tileLookup[2] = 1;
	tileLookup[8] = 2;
	tileLookup[10] = 3;
	tileLookup[11] = 4;
	tileLookup[16] = 5;
	tileLookup[18] = 6;
	tileLookup[22] = 7;
	tileLookup[24] = 8;
	tileLookup[26] = 9;
	tileLookup[27] = 10;
	tileLookup[30] = 11;
	tileLookup[31] = 12;
	tileLookup[64] = 13;
	tileLookup[66] = 14;
	tileLookup[72] = 15;
	tileLookup[74] = 16;
	tileLookup[75] = 17;
	tileLookup[80] = 18;
	tileLookup[82] = 19;
	tileLookup[86] = 20;
	tileLookup[88] = 21;
	tileLookup[90] = 22;
	tileLookup[91] = 23;
	tileLookup[94] = 24;
	tileLookup[95] = 25;
	tileLookup[104] = 26;
	tileLookup[106] = 27;
	tileLookup[107] = 28;
	tileLookup[120] = 29;
	tileLookup[122] = 30;
	tileLookup[123] = 31;
	tileLookup[126] = 32;
	tileLookup[127] = 33;
	tileLookup[208] = 34;
	tileLookup[210] = 35;
	tileLookup[214] = 36;
	tileLookup[216] = 37;
	tileLookup[218] = 38;
	tileLookup[219] = 39;
	tileLookup[222] = 40;
	tileLookup[223] = 41;
	tileLookup[248] = 42;
	tileLookup[250] = 43;
	tileLookup[251] = 44;
	tileLookup[254] = 45;
	tileLookup[255] = 46;	
	tileLookup[219] = 23;
	tileLookup[122] = 48;
}


/** Gpu assert helper */
void gpu_validate_assert(const char* message, const char* file, unsigned line, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{
    // Fill out error
    info->errorCode = SDKErr_Fail;
    info->errorMessage = message;
    info->errorSource = file;
    info->errorLine = line;
    info->headerSz = header->sz;
}


void gpu_error(struct GpuState_t* gpu, struct GpuInstance_t* job, int errorCode, const char* desc)
{
    if(desc)    
        printf("gpu err: %d : %s\n", errorCode, desc);
    else
        printf("gpu err: %d\n", errorCode);

    job->frameStats.cmdErrors++;
}


void gpu_cmd_impl_ResetGpu(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile)
{
    GPUCMD_ResetGpu* cmd = (GPUCMD_ResetGpu*)header;
    gpu_reset(gpu, cmd->cmds, cmd->buffers, cmd->stats);
}


bool validate_gpu_cmd_impl_ResetGpu(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{        
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_ResetGpu));                    
    return true;
}


void gpu_cmd_impl_FillRectCol(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GPUCMD_FillRectCol* cmd = (GPUCMD_FillRectCol*)header;
    uint16_t* pixels = (uint16_t*)fb->pixelsData;

    int pixelBaseX = cmd->x;
    int pixelBaseY = cmd->y - fb->y;
    int pixelStartY = 0;   
    if(pixelBaseY < 0)
        pixelStartY = -pixelBaseY;

    // calc start pixel X ()
    int pixelStartX = 0;   
    if(pixelBaseX < 0)
        pixelStartX = -pixelBaseX;
    int pixelCntY = cmd->h;
    
    // blend modes
    switch (cmd->blendMode)
    {   
        case EBlendMode_None: // overwrite
        {                            
            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY*fb->w);
                int pixelCntX = cmd->w;

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    pixels[ index ] = cmd->col;    
                    if(fb->attr)
                        fb->attr[index] = cmd->a;                    
                }
            }      
            break;
        }    
        case EBlendMode_Add: // Add blend
        {
            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY*fb->w);
                int pixelCntX = cmd->w;

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    Col16_t fillC;
                    fillC.value = cmd->col;

                    Col16_t c;
                    c.value = pixels[ index ];
                    int r = c.r + fillC.r;
                    int g = c.g + fillC.g;
                    int b = c.b + fillC.b;

                    c.r = MIN(r, COL16_MAX_R);
                    c.g = MIN(g, COL16_MAX_G);
                    c.b = MIN(b, COL16_MAX_B);

                    pixels[ index ] = c.value;        
                    if(fb->attr)
                        fb->attr[index] = cmd->a;  
                }
            }                                              
            break;
        }    
        case EBlendMode_Alpha: // Alpha blend
        {
            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY*fb->w);
                int pixelCntX = cmd->w;

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    pixels[index] = ALPHA_BLIT16_565(cmd->col, pixels[index], cmd->a);   
                    if(fb->attr)
                        fb->attr[index] = cmd->a;                           
                }
            }                                              
            break;
        }        
    }
}


bool validate_gpu_cmd_impl_FillRectCol(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_FillRectCol));                
    return true;
}


void gpu_cmd_impl_CreateBuffer_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile)
{
    // ignore buffer cmds on core 1
    if(job->instanceId > 0)
        return;
            
    if(tile->tileId != 0)
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "Buffer create can only occur on sycned tile 0");
        return;        
    }

    // Update buffer info in gpu state
    struct GPUCMD_CreateBuffer* cmd = (GPUCMD_CreateBuffer*)header;
    if(cmd->bufferId >= GPU_MAX_BUFFER_ID)
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "cmd->bufferId >= GPU_MAX_BUFFER_ID");
        return;
    }

    struct GpuBufferInfo* buffer = &gpu->buffers[cmd->bufferId];

    // validate arena
    uint32_t arenaSz = 0;
    uint8_t* basePtr = 0;
    switch (cmd->arena) 
    {
        case EGPUBufferArena_Ram0:
        {
            basePtr = gpu->ram0BufferBase + cmd->memOffset; 
            arenaSz = gpu->ram0BufferSz;
            break;
        }
        case EGPUBufferArena_Flash0:
        {  
            if( !gpu->flashStore )
            {
                gpu_error(gpu, job, EGpuErrorCode_OutOfBounds, "!gpu->flashStore");
                return;
            }

            if( gpu->flashStore->flashWriteCnt > GPU_FLASH_MAX_WRITE)
            {                
                gpu_error(gpu, job, EGpuErrorCode_FlashWriteMax, "gpu->flashWriteCnt > GPU_FLASH_MAX_WRITE");
                return; 
            }

            // validate page alignment for non sub buffers
            if(cmd->parentBufferId == 0xffff)
            {
                if((cmd->memOffset % GPU_FLASH_BUFFER_PAGE_SIZE) != 0)
                {
                    gpu_error(gpu, job, EGpuErrorCode_General, "flash based memOffset should be page aligned");
                    return; 
                }
            }

            basePtr = flash_store_get_ptr( gpu->flashStore, cmd->memOffset );
            if(!basePtr)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "create buffer overflow, memOffset+sz > flash size");
                return;                 
            }
            arenaSz = PICO_FLASH_SIZE_BYTES;

            // prepare temp buffer, clear to 1 so matched flash erased state
            memset(gpu->flashStore->tempFlashPage, 1, GPU_FLASH_BUFFER_PAGE_SIZE);

            // begin, assume writes next
            flash_store_begin( gpu->flashStore, cmd->memOffset );
            break;
        }        
        default:
            gpu_error(gpu, job, EGpuErrorCode_General, "Invalid arena");
            return;
    }

    // ensure fits into arena
    if(!cmd->memOffset + cmd->memSize >= arenaSz)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "!cmd->memOffset + cmd->memSize >= arenaSz");
        return;
    }

    // mark valid
    buffer->isValid = true;
    buffer->arenaId = cmd->arena;
    buffer->basePtr = basePtr;
    buffer->arenaOffset = cmd->memOffset; // offset in arena 
    buffer->size = cmd->memSize;
    buffer->textureFormat = cmd->textureFormat;
    buffer->w = cmd->w;
    buffer->h = cmd->h;
    buffer->locked = 0;
}


void gpu_cmd_impl_CreateBuffer(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile)
{
    mutex_enter_blocking(&gpu->bufferWriteLock);
    gpu_cmd_impl_CreateBuffer_impl(gpu, job, header, tile);
    mutex_exit(&gpu->bufferWriteLock);
}


bool validate_gpu_cmd_impl_CreateBuffer(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{   
    struct GPUCMD_CreateBuffer* cmd = (GPUCMD_CreateBuffer*)header;

    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_CreateBuffer));             
    GPU_VALIDATE_ASSERT(cmd->bufferId < GPU_MAX_BUFFER_ID);    
    return true;
}


bool toString_gpu_cmd_impl_CreateBuffer(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_CreateBuffer* cmd = (GPUCMD_CreateBuffer*)header;

    sprintf(buff, "bufferId:%d, arena:%d, memOffset:%d, memSize:%d, fmt:%d, w:%d, h:%d", 
            cmd->bufferId,
            cmd->arena,
            cmd->memOffset,
            cmd->memSize,    
            cmd->textureFormat,
            cmd->w,
            cmd->h
    );
    return true;
}


void gpu_cmd_impl_WriteBufferData_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile)
{
    // ignore buffer cmds on core 1
    if(job->instanceId > 0)
        return;

    struct GPUCMD_WriteBufferData* cmd = (GPUCMD_WriteBufferData*)header;

    if(tile->tileId != 0 && cmd->allowNonTileZero == 0)
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "Buffer writes can only occur on sycned tile 0");
        return;        
    }

    if(cmd->bufferId >= GPU_MAX_BUFFER_ID)
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "cmd->bufferId >= GPU_MAX_BUFFER_ID");
        return;
    }

    struct GpuBufferInfo* buffer = &gpu->buffers[cmd->bufferId];
    if(!buffer->isValid)
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "!buffer->isValid");
        return;
    }

    // check write bounds
    if(cmd->offset + cmd->dataSize > buffer->size || !buffer->basePtr)
    {
        gpu_error(gpu, job, EGpuErrorCode_OutOfBounds, "cmd->offset + cmd->dataSize > buffer->size || !buffer->basePtr");
        return;
    }

    // stats
    buffer->writeCnt++;
    if(cmd->flags & EGPUCMD_WriteBufferDataFlags_finalPage)
    {
        buffer->finalWriteCnt++;
        buffer->locked = 1;
    }

    // handle arena type
    switch (buffer->arenaId) 
    {
        case EGPUBufferArena_Ram0:
        {
            memcpy(buffer->basePtr + cmd->offset, cmd->data, cmd->dataSize );

            if(job->debugCommandUploads)
            {
                printf("[debug] WriteBufferData %d\n", cmd->bufferId);
            }
            break;
        }
        case EGPUBufferArena_Flash0:
        {                 
            if(cmd->dataSize > GPU_FLASH_BUFFER_PAGE_SIZE)
            {
                gpu_error(gpu, job, EGpuErrorCode_OutOfBounds, "cmd->dataSize > GPU_FLASH_BUFFER_PAGE_SIZE");
                return;
            }

            if( !gpu->flashStore )
            {
                gpu_error(gpu, job, EGpuErrorCode_OutOfBounds, "!gpu->flashStore");
                return;
            }

            // begin, assume writes next
            if(cmd->flags & EGPUCMD_WriteBufferDataFlags_firstPage)
                flash_store_begin( gpu->flashStore, buffer->arenaOffset + cmd->offset );

            int res = flash_store_next_write_block( gpu->flashStore, buffer->arenaOffset + cmd->offset, cmd->data, cmd->dataSize );   
            if(res != SDKErr_OK)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "flash_store_next_write_block failed");
                return;
            }

            if(cmd->flags & EGPUCMD_WriteBufferDataFlags_finalPage)
            {
                res = flash_store_end( gpu->flashStore );
                if(res != SDKErr_OK)
                {
                    gpu_error(gpu, job, EGpuErrorCode_General, "flash_store_end failed");
                    return;
                }            
            }     
            break;
        }        
        default:
            gpu_error(gpu, job, EGpuErrorCode_General, "Invalid arena");
            return;
    }
}


void gpu_cmd_impl_WriteBufferData(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile)
{
    mutex_enter_blocking(&gpu->bufferWriteLock);
    gpu_cmd_impl_WriteBufferData_impl(gpu, job, header, tile);
    mutex_exit(&gpu->bufferWriteLock);
}


bool validate_gpu_cmd_impl_WriteBufferData(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    struct GPUCMD_WriteBufferData* cmd = (GPUCMD_WriteBufferData*)header;
    GPU_VALIDATE_ASSERT(cmd->bufferId < GPU_MAX_BUFFER_ID);
    
    info->extraArg0 = cmd->bufferId;
    GPU_VALIDATE_ASSERT(header->sz >= sizeof(GPUCMD_WriteBufferData)); // Ensure min size
    
    info->expectedSize = cmd->dataSize + sizeof(GPUCMD_WriteBufferData); 
    info->extraCmdBaseSize = sizeof(GPUCMD_WriteBufferData);
    info->extraDataBaseSize = cmd->dataSize;

    GPU_VALIDATE_ASSERT(header->sz == cmd->dataSize + sizeof(GPUCMD_WriteBufferData)); // Ensure packet sized correctly
    
    return true;
}


bool toString_gpu_cmd_impl_WriteBufferData(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_WriteBufferData* cmd = (GPUCMD_WriteBufferData*)header;

    const char* isBufferValidStr = "[invalid]";
    if(cmd->bufferId < GPU_MAX_BUFFER_ID)
    {
        const struct GpuBufferInfo* buffer = &gpu->buffers[cmd->bufferId];   
        if(buffer->isValid)
            isBufferValidStr = ""; // clear for valid 
    }
    
    sprintf(buff, "bufferId:%d%s, flags:%d, offset:%d, dataSize:%d", 
            cmd->bufferId,
            isBufferValidStr,
            cmd->flags,
            cmd->offset,    
            cmd->dataSize
    );
    return true;
}


void gpu_cmd_impl_BlitRect16bpp(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    struct GPUCMD_BlitRect* cmd = (GPUCMD_BlitRect*)header;
    uint16_t* pixels = (uint16_t*)fb->pixelsData;
    if(cmd->bufferId >= GPU_MAX_BUFFER_ID)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    struct GpuBufferInfo* buffer = gpu_get_buffer_by_id(gpu, cmd->bufferId);
    if(!buffer)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    int pixelBaseX = cmd->dstX;
    int pixelBaseY = cmd->dstY - fb->y;
    int pixelStartY = 0;   
    if(pixelBaseY < 0)
        pixelStartY = -pixelBaseY;

    // calc start pixel X ()
    int pixelStartX = 0;   
    if(pixelBaseX < 0)
        pixelStartX = -pixelBaseX;
    int pixelCntY = cmd->h;
    
    // check for repeat modes
    bool wrapMode = false;
    if(cmd->w > buffer->w)
    {
        wrapMode = true;
    }

    switch (cmd->blendMode)
    {
    case EBlendMode_None:
    {
        if(buffer->textureFormat == ETextureFormat_RGB16)
        {
            uint16_t* srcBuffer = (uint16_t*)buffer->basePtr;

            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY * fb->w);
                int pixelCntX = cmd->w;
                int baseRowOffset = ( ( y + cmd->srcY ) * buffer->w );

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    uint32_t srcIndexX = x + cmd->srcX;

                    if(cmd->flags & EDrawTextureFlags_FlippedX)
                        srcIndexX = buffer->w - srcIndexX - 1;
                    
                    // wrap mode
                    if(wrapMode)
                        srcIndexX = srcIndexX % buffer->w;

                    if( srcIndexX >= buffer->size)
                        continue;

                    uint16_t c = srcBuffer[ srcIndexX + baseRowOffset ];

                    pixels[ index ] = c;
                    if(fb->attr)
                        fb->attr[index] = cmd->writeAlpha;
                }
            }   
        } 
        else if(buffer->textureFormat == ETextureFormat_8BPP)
        {
            // Get palette
            struct GpuBufferInfo* palBuffer = gpu_get_buffer_by_id(gpu, cmd->palBufferId);
            if(!palBuffer)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, ""); 
                return;
            }

            // Ensure valid palette format
            if(palBuffer->textureFormat != ETextureFormat_RGB16)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, ""); 
                return;
            }

            //uint8_t* srcBuffer = (uint16_t*)buffer->basePtr; // Compiled on gcc, changed on wasm might break things ?
            uint8_t* srcBuffer = (uint8_t*)buffer->basePtr;

            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY * fb->w);
                int pixelCntX = cmd->w;
                int baseRowOffset = ( ( y + cmd->srcY ) * buffer->w );

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    uint32_t srcIndexX = x + cmd->srcX;

                    if(cmd->flags & EDrawTextureFlags_FlippedX)
                        srcIndexX = buffer->w - srcIndexX - 1;
                    
                    // wrap mode
                    if(wrapMode)
                        srcIndexX = srcIndexX % buffer->w;

                    if( srcIndexX >= buffer->size)
                        continue;

                    uint8_t cid = srcBuffer[ srcIndexX + baseRowOffset ];

                    uint16_t c = ((uint16_t*)palBuffer->basePtr)[cid]; 

                    pixels[ index ] = c;
                    if(fb->attr)
                        fb->attr[index] = cmd->writeAlpha;
                }
            }  
        }
        break;
    }        
    case EBlendMode_ColorKey:
    {
        if(buffer->textureFormat == ETextureFormat_RGB16)
        {
            uint16_t* srcBuffer = (uint16_t*)buffer->basePtr;

            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY * fb->w);
                int pixelCntX = cmd->w;
                int baseRowOffset = ( ( y + cmd->srcY ) * buffer->w );

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    uint32_t srcIndexX = x + cmd->srcX;

                    if(cmd->flags & EDrawTextureFlags_FlippedX)
                        srcIndexX = buffer->w - srcIndexX - 1;
                        
                    if( srcIndexX >= buffer->size)
                        continue;

                    uint16_t c = srcBuffer[ srcIndexX + baseRowOffset ];

                    if(c != cmd->colKey)
                    {
                        pixels[ index ] = c;
                        if(fb->attr)
                            fb->attr[index] = cmd->writeAlpha;
                    }
                }
            }   
        } 
        else if(buffer->textureFormat == ETextureFormat_8BPP)
        {
            uint8_t* srcBuffer = (uint8_t*)buffer->basePtr;

            // Get palette
            struct GpuBufferInfo* palBuffer = gpu_get_buffer_by_id(gpu, cmd->palBufferId);
            if(!palBuffer)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, ""); 
                return;
            }

            // Ensure valid palette format
            if(palBuffer->textureFormat != ETextureFormat_RGB16)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, ""); 
                return;
            }

            for(int y=0;y<cmd->h;y++)
            {
                int localY = (cmd->dstY + y) - fb->y + cmd->srcY;
                if(localY < 0 || localY >= fb->h)
                    continue;

                for(int x=0;x<cmd->w;x++)
                {                    
                    int localX = cmd->dstX + x;
                    if(localX < 0 || localX >= fb->w)
                        continue;
                    
                    int index = localX + (localY*fb->w);
            
                    uint8_t cid = srcBuffer[x + cmd->srcX  + ((y+ cmd->srcY)*buffer->w)];
                    if(cid != cmd->colKey)
                    {
                        uint16_t c = ((uint16_t*)palBuffer->basePtr)[cid];                
                        pixels[ index ] = c;
                        if(fb->attr)
                            fb->attr[index] = 0xff;       
                    }
                }
            }
        }
        break;
    }
    case EBlendMode_ColorKeyTintAdd:
    {
        if(buffer->textureFormat == ETextureFormat_RGB16)
        {
            uint16_t* srcBuffer = (uint16_t*)buffer->basePtr;

            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY * fb->w);
                int pixelCntX = cmd->w;
                int baseRowOffset = ( ( y + cmd->srcY ) * buffer->w );

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    uint32_t srcIndexX = x + cmd->srcX;

                    if(cmd->flags & EDrawTextureFlags_FlippedX)
                        srcIndexX = buffer->w - srcIndexX - 1;
                        
                    if( srcIndexX >= buffer->size)
                        continue;

                    uint16_t c = srcBuffer[ srcIndexX + baseRowOffset ];

                    if(c != cmd->colKey)
                    {
                        c = gpu_col_add_uint16( c, cmd->palBufferId );   

                        pixels[ index ] =  c;
                        if(fb->attr)
                            fb->attr[index] = cmd->writeAlpha;
                    }
                }
            }   
        } 
        else if(buffer->textureFormat == ETextureFormat_8BPP)
        {
            uint8_t* srcBuffer = (uint8_t*)buffer->basePtr;

            // Get palette
            struct GpuBufferInfo* palBuffer = gpu_get_buffer_by_id(gpu, cmd->palBufferId);
            if(!palBuffer)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, ""); 
                return;
            }

            // Ensure valid palette format
            if(palBuffer->textureFormat != ETextureFormat_RGB16)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, ""); 
                return;
            }

            for(int y=0;y<cmd->h;y++)
            {
                int localY = (cmd->dstY + y) - fb->y + cmd->srcY;
                if(localY < 0 || localY >= fb->h)
                    continue;

                for(int x=0;x<cmd->w;x++)
                {                    
                    int localX = cmd->dstX + x;
                    if(localX < 0 || localX >= fb->w)
                        continue;
                    
                    int index = localX + (localY*fb->w);
            
                    uint8_t cid = srcBuffer[x + cmd->srcX  + ((y+ cmd->srcY)*buffer->w)];
                    if(cid != cmd->colKey)
                    {
                        uint16_t c = ((uint16_t*)palBuffer->basePtr)[cid];                
                        pixels[ index ] = c;
                        if(fb->attr)
                            fb->attr[index] = 0xff;       
                    }
                }
            }
        }
        break;
    }    
    case EBlendMode_FillMasked:
    {
        if(buffer->textureFormat == ETextureFormat_RGB16)
        {
            uint16_t* srcBuffer = (uint16_t*)buffer->basePtr;

            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY * fb->w);
                int pixelCntX = cmd->w;
                int baseRowOffset = ( ( y + cmd->srcY ) * buffer->w );

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    uint32_t srcIndexX = x + cmd->srcX;

                    if(cmd->flags & EDrawTextureFlags_FlippedX)
                        srcIndexX = buffer->w - srcIndexX - 1;
                        
                    if( srcIndexX >= buffer->size)
                        continue;

                    uint16_t c = srcBuffer[ srcIndexX + baseRowOffset ];

                    if(c != cmd->colKey)
                    {
                        pixels[ index ] = cmd->palBufferId; // fill masked
                        if(fb->attr)
                            fb->attr[index] = cmd->writeAlpha;
                    }
                }
            }   
        } 
        else if(buffer->textureFormat == ETextureFormat_8BPP)
        {
            uint8_t* srcBuffer = (uint8_t*)buffer->basePtr;

            for(int y=0;y<cmd->h;y++)
            {
                int localY = (cmd->dstY + y) - fb->y + cmd->srcY;
                if(localY < 0 || localY >= fb->h)
                    continue;

                for(int x=0;x<cmd->w;x++)
                {                    
                    int localX = cmd->dstX + x;
                    if(localX < 0 || localX >= fb->w)
                        continue;
                    
                    int index = localX + (localY*fb->w);
            
                    uint8_t cid = srcBuffer[x + cmd->srcX  + ((y+ cmd->srcY)*buffer->w)];
                    if(cid != cmd->colKey)
                    {
                        uint16_t c = cmd->palBufferId; // use as col
                        pixels[ index ] = c;
                        if(fb->attr)
                            fb->attr[index] = 0xff;       
                    }
                }
            }
        }
        break;        
    }
    case EBlendMode_ColkeyAlpha:
    {
        if(buffer->textureFormat == ETextureFormat_RGB16)
        {
            uint16_t* srcBuffer = (uint16_t*)buffer->basePtr;

            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;
                int pixelRowStride = (localY * fb->w);
                int pixelCntX = cmd->w;
                int baseRowOffset = ( ( y + cmd->srcY ) * buffer->w );

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    uint32_t srcIndexX = x + cmd->srcX;

                    if(cmd->flags & EDrawTextureFlags_FlippedX)
                        srcIndexX = buffer->w - srcIndexX - 1;
                        
                    if( srcIndexX >= buffer->size)
                        continue;

                    uint16_t col = srcBuffer[ srcIndexX + baseRowOffset ];  
                    if(col)
                    {
                        pixels[index] = ALPHA_BLIT16_565(col, pixels[index], cmd->colKey);                           
                        if(fb->attr)
                            fb->attr[index] = cmd->colKey;                      
                    }
                }
            }   
        } 
        break;
    }  
    case EBlendMode_Add:
    {
        if(buffer->textureFormat == ETextureFormat_RGB16)
        {
            uint16_t* srcBuffer = (uint16_t*)buffer->basePtr;

            for(int y=pixelStartY;y<pixelCntY;y++)
            {
                // calc start pixel Y
                int localY = pixelBaseY + y;
                if(localY >= fb->h)
                    break;

                int fillY = y;
                if(fillY >= buffer->h )
                    fillY = fillY % buffer->h;
                    
                int pixelRowStride = (localY * fb->w);
                int pixelCntX = cmd->w;
                int baseRowOffset = ( ( fillY + cmd->srcY ) * buffer->w );

                for(int x=pixelStartX;x<pixelCntX;x++)
                {                    
                    int localX = pixelBaseX + x;
                    if(localX >= fb->w)
                        break;

                    int index = localX + pixelRowStride;

                    uint32_t srcIndexX = x + cmd->srcX;
                    if(srcIndexX > buffer->w)
                        srcIndexX = srcIndexX % buffer->w;

                    if(cmd->flags & EDrawTextureFlags_FlippedX)
                        srcIndexX = buffer->w - srcIndexX - 1;
                        
                    if( srcIndexX >= buffer->size)
                        continue;

                    uint16_t c = srcBuffer[ srcIndexX + baseRowOffset ];
                    c = gpu_col_add_uint16(pixels[ index ], c );   
                    
                    pixels[ index ] = c;
                    if(fb->attr)
                        fb->attr[index] = cmd->writeAlpha;
                }
            }   
            break;
        }                 
    }            
    default:
        gpu_error(gpu, job, EGpuErrorCode_General, "Unsupported blit blend mode");
        return;            
    }
}


void gpu_cmd_impl_BlitRect8bpp(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    struct GPUCMD_BlitRect* cmd = (GPUCMD_BlitRect*)header;
    uint8_t* pixels = (uint8_t*)fb->pixelsData;
    if(cmd->bufferId >= GPU_MAX_BUFFER_ID)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    struct GpuBufferInfo* buffer = gpu_get_buffer_by_id(gpu, cmd->bufferId);
    if(!buffer)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    int pixelBaseX = cmd->dstX;
    int pixelBaseY = cmd->dstY - fb->y;
    int pixelStartY = 0;   
    if(pixelBaseY < 0)
        pixelStartY = -pixelBaseY;

    // calc start pixel X ()
    int pixelStartX = 0;   
    if(pixelBaseX < 0)
        pixelStartX = -pixelBaseX;
    int pixelCntY = cmd->h;

    switch (cmd->blendMode)
    {
    case EBlendMode_None:
    {
        if(buffer->textureFormat == ETextureFormat_8BPP)
        {
            uint8_t* srcBuffer = (uint8_t*)buffer->basePtr;

            for(int y=0;y<cmd->h;y++)
            {
                int localY = (cmd->dstY + y) - fb->y + cmd->srcY;
                if(localY < 0 || localY >= fb->h)
                    continue;

                for(int x=0;x<cmd->w;x++)
                {                    
                    int localX = cmd->dstX + x;
                    if(localX < 0 || localX >= fb->w)
                        continue;
                    
                    int index = localX + (localY*fb->w);
            
                    uint8_t cid = srcBuffer[x + cmd->srcX  + ((y+ cmd->srcY)*buffer->w)];
                    pixels[ index ] = cid;
                }
            }
        }
        break;
    }                
    default:
        gpu_error(gpu, job, EGpuErrorCode_General, "Unsupported blit blend mode");
        return;            
    }
}


void gpu_cmd_impl_BlitRect(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    if(fb->colorDepth == EColorDepth_BGR565)
    {
        gpu_cmd_impl_BlitRect16bpp( gpu, job, header, fb);
    }
    else if(fb->colorDepth == EColorDepth_8BPP)
    {
        gpu_cmd_impl_BlitRect8bpp( gpu, job, header, fb);
    }

}


bool validate_gpu_cmd_impl_BlitRect(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{
    struct GPUCMD_BlitRect* cmd = (GPUCMD_BlitRect*)header;

    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_BlitRect));            
    GPU_VALIDATE_ASSERT(cmd->bufferId < GPU_MAX_BUFFER_ID);
        
    info->extraArg0 = cmd->bufferId;
    //GPU_VALIDATE_ASSERT(info->buffersCreated[cmd->bufferId]);    // NOTE: buffers are global state
    GPU_VALIDATE_ASSERT(cmd->w < 1024);
    GPU_VALIDATE_ASSERT(cmd->h < 1024);
        
    return true;
}


bool toString_gpu_cmd_impl_BlitRect(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_BlitRect* cmd = (GPUCMD_BlitRect*)header;
    sprintf(buff, "bufferId:%d, srcX:%d, srcY:%d, dstX:%d, dstY:%d, w:%d, h:%d, colKey:%d, palBid:%d", 
            cmd->bufferId,
            cmd->srcX,
            cmd->srcY,    
            cmd->dstX,
            cmd->dstY,
            cmd->w,
            cmd->h,      
            cmd->colKey,
            cmd->palBufferId);   
    return true;
}


void gpu_cmd_impl_RegisterCmd(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile)
{
    GPUCMD_RegisterCmd* cmd = (GPUCMD_RegisterCmd*)header;
    
    if(gpu_register_cmd_jit(gpu, cmd->cmdId, cmd->bufferId) != SDKErr_OK)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "gpu_register_cmd_jit failed"); 
        return;
    }    
}


bool validate_gpu_cmd_impl_RegisterCmd(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_RegisterCmd));                
    return true;
}


void gpu_cmd_impl_SetDebug(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile)
{
    struct GPUCMD_SetDebug* cmd = (GPUCMD_SetDebug*)header;
    job->debugCommandUploads = cmd->dumpBufferUploads;
}


bool validate_gpu_cmd_impl_SetDebug(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_SetDebug));                
    return true;
}


#ifdef PICOCOM_SDL
// SDL link issues, inline speeds up pico so...
uint8_t getTileGroup(struct GpuBufferInfo* tilemapDataBuffer, int x, int y, struct GPUAttr_DrawTileMapData* tileInfos, uint8_t layerId, uint32_t tileInfoCnt, uint8_t tileIdMask )
#else
/*inlin*/uint8_t getTileGroup(struct GpuBufferInfo* tilemapDataBuffer, int x, int y, struct GPUAttr_DrawTileMapData* tileInfos, uint8_t layerId, uint32_t tileInfoCnt, uint8_t tileIdMask )
#endif
{
    uint8_t tileId = 0;
    if(x >= 0 && x < 16 && y >= 0 && y < 16) // local tile
    {
        if(!tilemapDataBuffer)
            return 0;

        uint8_t* tileData = tilemapDataBuffer->basePtr;
        uint32_t tileDataCnt = tilemapDataBuffer->size / sizeof(uint8_t);
        uint32_t index = x + (y * 16);
        if(index >= tileDataCnt)
            return 0;

        tileId = tileData[ index ] & tileIdMask;
    }
    else // edge tile
    {
        // resolve to vec
        int relChunkIdX = x < 0 ? -1 : (x >= 16 ? +1 : 0);
        int relChunkIdY = y < 0 ? -1 : (y >= 16 ? +1 : 0);

        struct GpuBufferInfo* edgeTilemapDataBuffer = tilemapDataBuffer->edgeData[ (relChunkIdX+1) + ((relChunkIdY+1) * 3)];
        if(!edgeTilemapDataBuffer)
            return 0;

	    // Get local
	    int subX = modi(x, 16);
	    int subY = modi(y, 16);   

        uint8_t* tileData = edgeTilemapDataBuffer->basePtr;
        uint32_t tileDataCnt = edgeTilemapDataBuffer->size / sizeof(uint8_t);
        uint32_t index = subX + (subY * 16);
        if(index >= tileDataCnt)
            return 0;

        tileId = tileData[ index ] & tileIdMask;
    }

    if(tileId >= tileInfoCnt)
        return 0;

    if(!tileInfos[ tileId ].isValid)
        return 0;

    return tileInfos[ tileId ].layers[layerId].tileGroup;
}


uint8_t getTileFluid(struct GpuBufferInfo* tilemapDataBuffer, struct GpuBufferInfo* massDataBuffer, int x, int y, struct GPUAttr_DrawTileMapData* tileInfos, uint32_t tileInfoCnt, uint8_t tileIdMask, uint8_t minMass )
{
    uint8_t tileId = 0;
    uint8_t mass = 0;
    if(x >= 0 && x < 16 && y >= 0 && y < 16) // local tile
    {
        if(!tilemapDataBuffer)
            return 0;

        uint8_t* tileData = tilemapDataBuffer->basePtr;
        uint8_t* massData = massDataBuffer->basePtr;
        uint32_t tileDataCnt = tilemapDataBuffer->size / sizeof(uint8_t);
        uint32_t index = x + (y * 16);
        if(index >= tileDataCnt)
            return 0;

        tileId = tileData[ index ] & tileIdMask;
        mass = massData[ index ];
    }
    else // edge tile
    {
        // resolve to vec
        int relChunkIdX = x < 0 ? -1 : (x >= 16 ? +1 : 0);
        int relChunkIdY = y < 0 ? -1 : (y >= 16 ? +1 : 0);

        struct GpuBufferInfo* edgeTilemapDataBuffer = tilemapDataBuffer->edgeData[ (relChunkIdX+1) + ((relChunkIdY+1) * 3)];
        if(!edgeTilemapDataBuffer)
            return 0;
        struct GpuBufferInfo* edgeMassDataBuffer = massDataBuffer->edgeData[ (relChunkIdX+1) + ((relChunkIdY+1) * 3)];
        if(!edgeMassDataBuffer)
            return 0;

	    // Get local
	    int subX = modi(x, 16);
	    int subY = modi(y, 16);   

        uint8_t* tileData = edgeTilemapDataBuffer->basePtr;
        uint8_t* massData = edgeMassDataBuffer->basePtr;
        uint32_t tileDataCnt = edgeTilemapDataBuffer->size / sizeof(uint8_t);
        uint32_t index = subX + (subY * 16);
        if(index >= tileDataCnt)
            return 0;

        tileId = tileData[ index ] & tileIdMask;
        mass = massData[ index ];
    }

    if(tileId >= tileInfoCnt)
        return 0;

    if(!tileInfos[ tileId ].isFluid)
        return 0;

    if(mass < minMass)
        mass = minMass;
    return mass;
}


void gpu_cmd_impl_DrawTileMap(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GPUCMD_DrawTileMap* cmd = (GPUCMD_DrawTileMap*)header;
    uint16_t* pixels = (uint16_t*)fb->pixelsData;

    struct GpuBufferInfo* tilemapDataBuffer = gpu_get_buffer_by_id(gpu, cmd->tilemapDataBufferId);
    if(!tilemapDataBuffer)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    struct GpuBufferInfo* stateDataBuffer = gpu_get_buffer_by_id(gpu, cmd->tilemapStateBufferId);
    if(!stateDataBuffer)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    struct GpuBufferInfo* tilemapAttribBuffer = gpu_get_buffer_by_id(gpu, cmd->tilemapAttribBufferId);
    if(!tilemapAttribBuffer)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    // get tile data
    struct GPUAttr_DrawTileMapData* tileInfos = (struct GPUAttr_DrawTileMapData*)tilemapAttribBuffer->basePtr;
    uint32_t tileInfosCnt = tilemapAttribBuffer->size / sizeof(struct GPUAttr_DrawTileMapData);

    uint8_t* tileData = tilemapDataBuffer->basePtr;
    uint32_t tileDataCnt = tilemapDataBuffer->size / sizeof(uint8_t);

    uint8_t* stateData = stateDataBuffer->basePtr;
    uint32_t stateDataCnt = stateDataBuffer->size / sizeof(uint8_t);
        

    uint32_t localTileX = FLOOR_INT(-cmd->x, GPU_TILEMAP_TILE_SZ);
    uint32_t cmdTileCntX = CEIL_INT(cmd->x+cmd->w, GPU_TILEMAP_TILE_SZ);
    if(localTileX > GPU_TILEMAP_TILE_SZ)
        localTileX = 0;
    if(cmdTileCntX > GPU_TILEMAP_TILE_SZ)
        cmdTileCntX = GPU_TILEMAP_TILE_SZ; 

    uint32_t localTileY = FLOOR_INT(-(cmd->y - fb->y), GPU_TILEMAP_TILE_SZ);
    uint32_t cmdTileCntY = CEIL_INT(cmd->y+cmd->h, GPU_TILEMAP_TILE_SZ);
    if(localTileY > GPU_TILEMAP_TILE_SZ)
        localTileY = 0;
    if(cmdTileCntY > GPU_TILEMAP_TILE_SZ)
        cmdTileCntY = GPU_TILEMAP_TILE_SZ; 

    // Tile y loop    
    for(int tileY=localTileY;tileY<=localTileY+cmdTileCntY;tileY++)
    {
        int tileStrideX = (tileY * GPU_TILEMAP_TILE_SZ);
        int tileBaseY = tileY * GPU_TILEMAP_TILE_SZ;
        int pixelBaseY = (cmd->y + tileBaseY) - fb->y;

        int pixelStartY = 0;   
        if(pixelBaseY < 0)
            pixelStartY = -pixelBaseY;

        // tile X loop        
        for(int tileX=localTileX;tileX<localTileX+cmdTileCntX;tileX++)
        {
            int tileBaseX = tileX * GPU_TILEMAP_TILE_SZ;            
            int pixelBaseX = cmd->x + tileBaseX;
            
            // calc start pixel X ()
            int pixelStartX = 0;   
            if(pixelBaseX < 0)
                pixelStartX = -pixelBaseX;
            int pixelCntY = GPU_TILEMAP_TILE_SZ;

            uint8_t tileId = tileData[ tileX + tileStrideX ];
            uint8_t state = stateData[ tileX + tileStrideX ];
            struct GPUAttr_DrawTileMapData* tileInfo = &tileInfos[ tileId & cmd->tileIdMask ];
            if(!tileInfo->isValid)
                continue;
            
            if(!(tileInfo->tileGroup & cmd->tileGroupMask))
                continue;
            
            // render each layer
            uint8_t tileWriteMask[16*16] = {0};

            for(int layerId=0;layerId<tileInfo->layerCnt;layerId++)
            {
                struct GPUAttr_TileDataLayer* layer = &tileInfo->layers[layerId];

                // resolve frame id
                // TODO: cache this as same per layer

                uint32_t frameOffset = 0;
                if(layer->frameSeed)
                {
                    struct pseudo_random_t rng;
                    pseudo_random_init(&rng); 
                    pseudo_random_offset_seed(&rng, cmd->seed, cmd->seed); 
                    pseudo_random_offset_seed(&rng, tileX, tileY); 
                    frameOffset = pseudo_random_uint_range(&rng, 0, layer->tileFrameCnt);
                }

                uint8_t frameId = 0xff;
                if(layer->tileFrameCnt && layer->animFrameTime != 0)                 
                    frameId = layer->frameSeed + ( ((int)(cmd->time / layer->animFrameTime) + frameOffset) % layer->tileFrameCnt );            
                
                struct GPUAttr_TileFrame* frame = &layer->tileFrames[frameId < layer->tileFrameCnt ? frameId : 0];
                
                switch(layer->type)
                {
                    case EGPUAttr_ETileRenderType_AutoTile:
                    {
                        // get tilemap texture
                        if(layer->textureBufferId >= GPU_MAX_BUFFER_ID)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "");
                            return;
                        }

                        // base tilemap texture
                        struct GpuBufferInfo* baseTextureBuffer = &gpu->buffers[layer->textureBufferId];
                        if(!baseTextureBuffer->isValid)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "Tilemap missing layer texture");
                            return;
                        }

                        if(!baseTextureBuffer->basePtr)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "");
                            return;
                        }   
                        uint16_t* srcBaseBuffer = (uint16_t*)baseTextureBuffer->basePtr;

                        // resolve auto tile, sample tile (accross edges/chunks)
                        uint32_t northWestVal = getTileGroup(tilemapDataBuffer, tileX - 1, tileY - 1, tileInfos, layerId, tileInfosCnt, cmd->tileIdMask) == layer->tileGroup;
                        uint32_t northVal = getTileGroup(tilemapDataBuffer, tileX, tileY - 1, tileInfos, layerId, tileInfosCnt, cmd->tileIdMask) == layer->tileGroup;
                        uint32_t northEastVal = getTileGroup(tilemapDataBuffer, tileX + 1, tileY - 1, tileInfos, layerId, tileInfosCnt, cmd->tileIdMask) == layer->tileGroup;
                        uint32_t westVal = getTileGroup(tilemapDataBuffer, tileX - 1, tileY, tileInfos, layerId, tileInfosCnt, cmd->tileIdMask) == layer->tileGroup;
                        uint32_t eastVal = getTileGroup(tilemapDataBuffer, tileX + 1, tileY, tileInfos, layerId, tileInfosCnt, cmd->tileIdMask) == layer->tileGroup;
                        uint32_t southWestVal = getTileGroup(tilemapDataBuffer, tileX - 1, tileY + 1, tileInfos, layerId, tileInfosCnt, cmd->tileIdMask) == layer->tileGroup;
                        uint32_t southVal = getTileGroup(tilemapDataBuffer, tileX, tileY + 1, tileInfos, layerId, tileInfosCnt, cmd->tileIdMask) == layer->tileGroup;
                        uint32_t southEastVal = getTileGroup(tilemapDataBuffer, tileX + 1, tileY + 1, tileInfos, layerId, tileInfosCnt, cmd->tileIdMask) == layer->tileGroup;

                        if (northVal == 0 || westVal == 0)
                            northWestVal = 0;

                        if (northVal == 0 || eastVal == 0)
                            northEastVal = 0;

                        if (southVal == 0 || westVal == 0)
                            southWestVal = 0;

                        if (southVal == 0 || eastVal == 0)
                            southEastVal = 0;

                        uint32_t autoTileId = (northWestVal * 1) + (northVal * 2) + (northEastVal * 4) + (westVal * 8) + (eastVal * 16) + (southWestVal * 32) + (southVal * 64) + (southEastVal * 128);

                        int resolvedTileId = tileLookup[autoTileId];
                        int resolvedTileX = resolvedTileId % 8;
                        int resolvedTileY = FLOOR_INT(resolvedTileId, 8.0f);

                        int tilebasePxX = (resolvedTileX * 16) + frame->x;
                        int tilebasePxY = (resolvedTileY * 16) + frame->y;

                        // pixel y loop
                        for(int y=pixelStartY;y<pixelCntY;y++)
                        {
                            // calc start pixel Y
                            int localY = pixelBaseY + y;
                            if(localY >= fb->h)
                                break;
                            int pixelRowStride = (localY*fb->w);
                            int srcStride = ( (tilebasePxY + y) * baseTextureBuffer->w);
                            int pixelCntX = GPU_TILEMAP_TILE_SZ;

                            for(int x=pixelStartX;x<pixelCntX;x++)
                            {                    
                                int localX = pixelBaseX + x;
                                if(localX >= fb->w)
                                    break;

                                int index = localX + pixelRowStride;

                                uint16_t col = srcBaseBuffer[ (tilebasePxX + x) + srcStride];                                
                                if(col)
                                {
                                    pixels[ index  ] = col;
                                    if(fb->attr)
                                        fb->attr[index] = GPU_ATTR_ALPHA_MASK;                                                
                                    
                                    // overlay masker
                                    tileWriteMask[ x + (y*16)] = 1;
                                }
                            }
                        }                              
                        break;
                    }                   
                    case EGPUAttr_ETileRenderType_Decals:
                    {
                        // get tilemap texture
                        if(layer->textureBufferId >= GPU_MAX_BUFFER_ID)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "");
                            return;
                        }

                        // base tilemap texture
                        struct GpuBufferInfo* baseTextureBuffer = &gpu->buffers[layer->textureBufferId];
                        if(!baseTextureBuffer->isValid)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "Tilemap missing layer texture");
                            return;
                        }

                        if(!baseTextureBuffer->basePtr)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "");
                            return;
                        }

                        uint16_t* srcBaseBuffer = (uint16_t*)baseTextureBuffer->basePtr;
                        
                        struct pseudo_random_t rng;
                        pseudo_random_init(&rng); 
                        pseudo_random_offset_seed(&rng, tileX, tileY); 
                    
                        int resolvedTileX = pseudo_random_uint_range(&rng, 0, 8);
                        int resolvedTileY = pseudo_random_uint_range(&rng, 0, 8);

                        int tilebasePxX = (resolvedTileX * 16) + frame->x;
                        int tilebasePxY = (resolvedTileY * 16) + frame->y;

                        // pixel y loop
                        for(int y=pixelStartY;y<pixelCntY;y++)
                        {
                            // calc start pixel Y
                            int localY = pixelBaseY + y;
                            if(localY >= fb->h)
                                break;
                            int pixelRowStride = (localY*fb->w);

                            int pixelCntX = GPU_TILEMAP_TILE_SZ;

                            for(int x=pixelStartX;x<pixelCntX;x++)
                            {                    
                                int localX = pixelBaseX + x;
                                if(localX >= fb->w)
                                    break;

                                int index = localX + pixelRowStride;

                                if( layer->maskPrevLayer)
                                {
                                    if(!tileWriteMask[x + (y*16)])
                                        continue;
                                }

                                uint16_t col = srcBaseBuffer[ (tilebasePxX + x) + ( (tilebasePxY + y) * baseTextureBuffer->w)];                                
                                if(col)
                                {
                                    pixels[ index  ] = col;
                                    if(fb->attr)
                                        fb->attr[index] = 0xff;                                
                                }
                            }
                        }                              
                        break;
                    } 
                    case EGPUAttr_ETileRenderType_DecalsFromMass:
                    {
                        // get tilemap texture
                        if(layer->textureBufferId >= GPU_MAX_BUFFER_ID)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "");
                            return;
                        }

                        // base tilemap texture
                        struct GpuBufferInfo* baseTextureBuffer = &gpu->buffers[layer->textureBufferId];
                        if(!baseTextureBuffer->isValid)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "Tilemap missing layer texture");
                            return;
                        }

                        if(!baseTextureBuffer->basePtr)
                        {
                            gpu_error(gpu, job, EGpuErrorCode_General, "");
                            return;
                        }

                        uint16_t* srcBaseBuffer = (uint16_t*)baseTextureBuffer->basePtr;
                        
                        struct pseudo_random_t rng;
                        pseudo_random_init(&rng); 
                        pseudo_random_offset_seed(&rng, tileX, tileY); 
                    
                        int rngVariant = pseudo_random_uint_range(&rng, 0, layer->numHTiles);
                        if(rngVariant >= layer->numHTiles)
                            rngVariant = layer->numHTiles-1;
                        
                        int tilebasePxX = rngVariant*16;

                        // state
                        uint8_t invState = 255-state;
                        if( invState < layer->minState )
                            continue;
                        int frameId = (invState-layer->minState) / layer->stateDiv;
                        if(frameId >= layer->tileFrameCnt)
                            frameId = layer->tileFrameCnt-1;
                        int tilebasePxY = frameId * 16;

                        // pixel y loop
                        for(int y=pixelStartY;y<pixelCntY;y++)
                        {
                            // calc start pixel Y
                            int localY = pixelBaseY + y;
                            if(localY >= fb->h)
                                break;
                            int pixelRowStride = (localY*fb->w);

                            int pixelCntX = GPU_TILEMAP_TILE_SZ;

                            for(int x=pixelStartX;x<pixelCntX;x++)
                            {                    
                                int localX = pixelBaseX + x;
                                if(localX >= fb->w)
                                    break;

                                int index = localX + pixelRowStride;

                                if( layer->maskPrevLayer)
                                {
                                    if(!tileWriteMask[x + (y*16)])
                                        continue;
                                }

                                uint16_t col = srcBaseBuffer[ (tilebasePxX + x) + ( (tilebasePxY + y) * baseTextureBuffer->w)];                                
                                if(col)
                                {
                                    pixels[ index  ] = col;
                                    if(fb->attr)
                                        fb->attr[index] = state;                                
                                }
                            }
                        }                              
                        break;
                    }                    
                } // layer

            }// for
        }
    }    
}


void gpu_cmd_impl_CreateLinkedTilemapBuffer(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GPUCMD_CreateLinkedTilemapBuffer* cmd = (GPUCMD_CreateLinkedTilemapBuffer*)header;

    if(cmd->tilemapDataBufferId >= GPU_MAX_BUFFER_ID)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    struct GpuBufferInfo* tilemapDataBuffer = &gpu->buffers[cmd->tilemapDataBufferId];
    if(!tilemapDataBuffer->isValid)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    if(!tilemapDataBuffer->basePtr)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    struct GpuBufferInfo* stateDataBuffer = &gpu->buffers[cmd->tilemapStateBufferId];
    if(!stateDataBuffer->isValid)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    if(!stateDataBuffer->basePtr)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    // cache each buffer ( rather than base ptr as buffers are never reallocated and safe to ref )
    for(int i=0;i<9;i++)
    {
        uint16_t tilemapBufferId = cmd->edgeTilemapDataBufferIds[i];
        uint16_t stateBufferId = cmd->edgeTilemapStateBufferIds[i];

        // ignore invalid/excluded
        if(tilemapBufferId != 0xffff)
        {
            // ensure in range
            if(cmd->tilemapDataBufferId >= GPU_MAX_BUFFER_ID)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "");
                return;
            }

            // ensure valid
            if(!gpu->buffers[tilemapBufferId].isValid)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "");
                return;
            }

            tilemapDataBuffer->edgeData[i] = &gpu->buffers[tilemapBufferId];
        }

        // ignore invalid/excluded
        if(stateBufferId != 0xffff)
        {
            // ensure in range
            if(cmd->tilemapStateBufferId >= GPU_MAX_BUFFER_ID)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "");
                return;
            }

            // ensure valid
            if(!gpu->buffers[stateBufferId].isValid)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "");
                return;
            }

            stateDataBuffer->edgeData[i] = &gpu->buffers[stateBufferId];
        }
    }
}


void gpu_cmd_impl_DrawWater(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GPUCMD_DrawTileMap* cmd = (GPUCMD_DrawTileMap*)header;
    uint16_t* pixels = (uint16_t*)fb->pixelsData;

    struct GpuBufferInfo* tilemapDataBuffer = gpu_get_buffer_by_id(gpu, cmd->tilemapDataBufferId);
    if(!tilemapDataBuffer)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    struct GpuBufferInfo* stateDataBuffer = gpu_get_buffer_by_id(gpu, cmd->tilemapStateBufferId);
    if(!stateDataBuffer)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    struct GpuBufferInfo* tilemapAttribBuffer = gpu_get_buffer_by_id(gpu, cmd->tilemapAttribBufferId);
    if(!tilemapAttribBuffer)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "");
        return;
    }

    // get tile data
    struct GPUAttr_DrawTileMapData* tileInfos = (struct GPUAttr_DrawTileMapData*)tilemapAttribBuffer->basePtr;
    uint32_t tileInfosCnt = tilemapAttribBuffer->size / sizeof(struct GPUAttr_DrawTileMapData);

    uint8_t* tileData = tilemapDataBuffer->basePtr;
    uint32_t tileDataCnt = tilemapDataBuffer->size / sizeof(uint8_t);

    uint8_t* stateData = stateDataBuffer->basePtr;
    uint32_t stateDataCnt = stateDataBuffer->size / sizeof(uint8_t);

    struct pseudo_random_t rng;
    pseudo_random_init(&rng); 
    pseudo_random_offset_seed(&rng, cmd->seed, cmd->seed); 

    uint32_t localTileX = FLOOR_INT(-cmd->x, GPU_TILEMAP_TILE_SZ);
    uint32_t cmdTileCntX = CEIL_INT(cmd->x+cmd->w, GPU_TILEMAP_TILE_SZ);
    if(localTileX > GPU_TILEMAP_TILE_SZ)
        localTileX = 0;
    if(cmdTileCntX > GPU_TILEMAP_TILE_SZ)
        cmdTileCntX = GPU_TILEMAP_TILE_SZ; 

    uint32_t localTileY = FLOOR_INT(-(cmd->y - fb->y), GPU_TILEMAP_TILE_SZ);
    uint32_t cmdTileCntY = CEIL_INT(cmd->y+cmd->h, GPU_TILEMAP_TILE_SZ);    

    if(localTileY > GPU_TILEMAP_TILE_SZ)
        localTileY = 0;
    if(cmdTileCntY > GPU_TILEMAP_TILE_SZ)
        cmdTileCntY = GPU_TILEMAP_TILE_SZ; 

    int tileYCnt = localTileY+ ((FRAME_TILE_SZ_Y / 16));

    // Tile y loop    
    for(int tileY=localTileY;tileY<=tileYCnt;tileY++)
    {
        int tileStrideX = (tileY * GPU_TILEMAP_TILE_SZ);
        int tileBaseY = tileY * GPU_TILEMAP_TILE_SZ;
        int pixelBaseY = (cmd->y + tileBaseY) - fb->y;

        int pixelStartY = 0;   
        if(pixelBaseY < 0)
            pixelStartY = -pixelBaseY;

        if(pixelStartY > FRAME_TILE_SZ_Y)
            break;

        // tile X loop        
        for(int tileX=localTileX;tileX<localTileX+cmdTileCntX;tileX++)
        {       
            int tileBaseX = tileX * GPU_TILEMAP_TILE_SZ;            
            int pixelBaseX = cmd->x + tileBaseX;
            
            // calc start pixel X ()
            int pixelStartX = 0;   
            if(pixelBaseX < 0)
                pixelStartX = -pixelBaseX;
            int pixelCntY = GPU_TILEMAP_TILE_SZ;

            if(pixelStartX > FRAME_W)
                break;
            
            uint8_t tileId = tileData[ tileX + (tileY*16) ];            
            uint8_t state = stateData[ tileX + (tileY*16) ];
            struct GPUAttr_DrawTileMapData* tileInfo = &tileInfos[ tileId & cmd->tileIdMask ];
            if(!tileInfo->isValid)
                continue;
            struct GPUAttr_DrawTileMapData* overlayTileInfo = &tileInfos[ tileInfo->overlayTileId ];
            if(!overlayTileInfo->isValid)
                continue;                
            
            if(!(tileInfo->tileGroup & cmd->tileGroupMask))
                continue;                
            
            // layer determins state level
            int layerId = (state / 255.0f) * tileInfo->layerCnt;
            if(layerId >= tileInfo->layerCnt)
                layerId = tileInfo->layerCnt - 1;

            // detect edges type
            uint8_t fluidAbove = getTileFluid( tilemapDataBuffer, stateDataBuffer, tileX + 0, tileY - 1, tileInfos, tileInfosCnt, cmd->tileIdMask, 1 );
            uint8_t fluidBelow = getTileFluid( tilemapDataBuffer, stateDataBuffer, tileX + 0, tileY + 1, tileInfos, tileInfosCnt, cmd->tileIdMask, 1 );
            if(fluidAbove != 0)
            {                
                layerId = 7; // fill above
            }

            if( layerId >= NUM_ELEMS(tileInfo->layers))
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "layerId >= NUM_ELEMS(tileInfo->layers)" );
                return;
            }
            
            struct GPUAttr_TileDataLayer* layer = &tileInfo->layers[layerId];

            // get anim frame
            uint32_t frameOffset = 0;
            if(layer->frameSeed)
            {
                frameOffset = pseudo_random_uint_range(&rng, 0, layer->tileFrameCnt);
            }

            uint8_t frameId = 0xff;
            if(layer->tileFrameCnt && layer->animFrameTime != 0)                 
                frameId = layer->frameSeed + ( ((int)(cmd->time / layer->animFrameTime) + frameOffset) % layer->tileFrameCnt );                              
 
            struct GPUAttr_TileFrame* frame = &layer->tileFrames[frameId <= layer->tileFrameCnt ? frameId : 0];            

            if( layer->textureBufferId >= NUM_ELEMS(gpu->buffers) )
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "layer->textureBufferId >= NUM_ELEMS(gpu->buffers)");
                return;
            }

            // base tilemap texture
            struct GpuBufferInfo* baseTextureBuffer =  &gpu->buffers[ layer->textureBufferId ]; //gpu_get_buffer_by_id( gpu, layer->textureBufferId );
            if(!baseTextureBuffer)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "Tilemap missing layer texture");
                return;
            }
            if(baseTextureBuffer->textureFormat != ETextureFormat_8BPP)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "Water tilemaps requires 8BPP mapped tiles");
                return;
            }
            uint8_t* srcBaseBuffer = (uint8_t*)baseTextureBuffer->basePtr;


            struct GpuBufferInfo* palBuffer =  &gpu->buffers[layer->palBufferId ];; //gpu_get_buffer_by_id( gpu, layer->palBufferId );
            if(!palBuffer)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "Tilemap missing layer texture");
                return;
            }
            if(palBuffer->textureFormat != ETextureFormat_RGB16)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "Water tilemaps palette requires 16BPP");
                return;
            }

            uint32_t palDataSz = palBuffer->size / sizeof(uint16_t);
            uint16_t* palData = (uint16_t*)palBuffer->basePtr;

            // get overlay 
            uint32_t overlayVariantLayerId = 0;
            struct GPUAttr_TileDataLayer* overlayLayer = &overlayTileInfo->layers[overlayVariantLayerId];

            struct GpuBufferInfo* overlayTextureBuffer =  &gpu->buffers[overlayLayer->textureBufferId ]; //gpu_get_buffer_by_id(gpu, overlayLayer->textureBufferId);
            if(!overlayTextureBuffer)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "Overlay missing layer texture");
                return;
            }
            uint8_t* overlayBaseBuffer = (uint8_t*)overlayTextureBuffer->basePtr;

            uint32_t overlayFrameOffset = 0;
            if(overlayLayer->frameSeed)
            {
               overlayFrameOffset = pseudo_random_uint_range(&rng, 0, overlayLayer->tileFrameCnt);
            }

            uint8_t overlayFrameId = 0xff;
            if(overlayLayer->tileFrameCnt && overlayLayer->animFrameTime != 0)            
            {     
                overlayFrameId =  overlayLayer->frameSeed + ( ((int)(cmd->time / overlayLayer->animFrameTime) + overlayFrameOffset) );               
                overlayFrameId = overlayFrameId % overlayLayer->tileFrameCnt;
            }

            struct GPUAttr_TileFrame* overlayFrame = &overlayLayer->tileFrames[overlayFrameId < overlayLayer->tileFrameCnt ? overlayFrameId : 0];                        

            // TODO: tile attr
            int overdrawOffsetSub = 4;

            for(int y=0;y<16+(overdrawOffsetSub*2);y++)            
            {                        
                int yy = y-overdrawOffsetSub;
                int localY = pixelBaseY + yy;
                if(localY >= (int)fb->h)
                {
                    break;
                }
                if(localY < 0)
                    continue;

                for(int x=0;x<16+(overdrawOffsetSub*2);x++)                
                {                              
                    uint8_t colId = srcBaseBuffer[ x + (16-overdrawOffsetSub)  + ((frame->y + y + (16-overdrawOffsetSub)) * baseTextureBuffer->w) ];                    
                    if(colId > palDataSz)
                        colId = 0;

                    // wave replace
                    if(!(fluidAbove == 0))
                    {
                        if((colId ==1))
                        {
                            colId = 2;
                        }
                    }

                    uint16_t col = palData[colId];      
                    uint16_t overlayCol = palData[ overlayBaseBuffer[ x + (16-overdrawOffsetSub) + ((overlayFrame->y + y + (16-overdrawOffsetSub)) * overlayTextureBuffer->w) ] ];
                    
                    if(col)
                    {                           
                        int xx = x-overdrawOffsetSub;

                        int pixelRowStride = (localY * fb->w);
                        int localX = pixelBaseX + xx;
                        if(localX >= fb->w)
                            continue;   // break -> speed up but loss

                        int index = localX + pixelRowStride;                        

                        // blend policy
                        if(fb->attr[ index ] & GPU_ATTR_PIXELWRITE0_MASK) // water pixel already written
                        {
                            continue; // reject
                        }
                        else
                        {                   
                            // Alpha                            
                            uint8_t a = tileInfo->writeAlpha | GPU_ATTR_PIXELWRITE0_MASK;

                            // Addive
                            if( tileInfo->blendAdditive)
                            {
                                col = gpu_col_add_uint16(pixels[ index ], col );   
                                
                                if((fb->attr[index] & GPU_ATTR_ALPHA_MASK) == 0)
                                {
                                    // set alpha bits if transparent
                                    fb->attr[index] = a | GPU_ATTR_PIXELWRITE0_MASK;
                                }
                            } 
                            else
                            {                                
                                // blend into fb
                                col = ALPHA_BLIT16_565(col, pixels[index], a);

                                if((fb->attr[index] & GPU_ATTR_ALPHA_MASK) == 0)
                                {
                                    // set alpha bits if transparent
                                    fb->attr[index] = a | GPU_ATTR_PIXELWRITE0_MASK;
                                }
                            }

                            // enforce hilites
                            if(overlayCol)
                            {
                                col = gpu_col_add_uint16(pixels[ index ], overlayCol );                        
                            }
                            
                            if(colId == 1) // wave overlay
                            {
                                col = gpu_col_add_uint16(pixels[ index ], col );                      
                            }

                            pixels[index] = col;                           
                        }
                    }
                }
            }
        }
    }    
}


void gpu_diag_buffers(GpuState_t* gpu)
{
    printf("gpu_diag_buffers\n");

    // dump all buffers
    for(int i=0;i<GPU_MAX_BUFFER_ID;i++)
    {
        struct GpuBufferInfo* a = &gpu->buffers[i];
        if(!a->isValid)
            continue;
        printf("\tbuffer[%d] type: %s, offset: %d, size: %d" "\n", i, a->arenaId == 0 ? "Ram" : "Flash", a->arenaOffset, a->size);            
    }

    // check overlap
    for(int i=0;i<GPU_MAX_BUFFER_ID;i++)
    {
        struct GpuBufferInfo* a = &gpu->buffers[i];
        if(!a->isValid)
            continue;
        for(int j=0;j<GPU_MAX_BUFFER_ID;j++)
        {
            struct GpuBufferInfo* b = &gpu->buffers[j];
            if(!b->isValid)
                continue;
            if( i == j)
                continue;

            if( a->arenaId == b->arenaId)
            {
                if( a->arenaOffset < b->arenaOffset + b->size && a->arenaOffset + a->size > b->arenaOffset)
                {
                    printf("\tbuffer overlap bufferId: %d with other %d\n", i, j);
                    printf("\t\t" "a.offset: %d, a.size: %d" "\n", a->arenaOffset, a->size);
                    printf("\t\t" "b.offset: %d, b.size: %d" "\n", b->arenaOffset, b->size);
                }
            }           
        }
    }

}


void gpu_cmd_impl_CompositeTile(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    if(!job->fb0)
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "Missing fb0");
        return; 
    }

    GPUCMD_CompositeTile* cmd = (GPUCMD_CompositeTile*)header;

    if(job->fb0->colorDepth == EColorDepth_BGR565)
    {
        uint16_t* dstRow = (uint16_t*)fb->pixelsData;
        uint16_t* srcPixelsRow = (uint16_t*)job->fb0->pixelsData;
        uint8_t* attrPixelsRow = job->fb0->attr;    

        switch (cmd->blendMode)
        {
        case EBlendMode_None:       
        {
            for(int i=0;i<FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE;i++)
                dstRow[i] = *(srcPixelsRow++);
            break;
        }
        case EBlendMode_ColorKey:       
        {
            for(int i=0;i<FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE;i++)
            {
                uint16_t c = *(srcPixelsRow++);
                if(c)
                {
                    dstRow[i] = c;
                }            
            }
            break;
        }      
        case EBlendMode_Alpha:       
        {        
            if(!attrPixelsRow)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "Missing src alpha");
                return; 
            }

            for(int i=0;i<FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE;i++)
            {
                uint16_t c = *(srcPixelsRow++);
                uint8_t attr = *(attrPixelsRow++);
                uint8_t a = (attr & GPU_ATTR_ALPHA_MASK) * 17;
                
                dstRow[i] = ALPHA_BLIT16_565(c, dstRow[i], a);            
            }
            break;
        }
        case EBlendMode_DebugAttrAlpha:
        {
            if(!attrPixelsRow)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "Missing src alpha");
                return; 
            }

            for(int i=0;i<FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE;i++)
            {            
                uint8_t attr = *(attrPixelsRow++);
                uint8_t a = (attr & GPU_ATTR_ALPHA_MASK) * 16;

                // debug alpha
                dstRow[i] = gpu_col_from_rgbf(a/255.0f, 0, 0).value;  
            }
            break;
        }       
        case EBlendMode_DebugAttrPixelWriteMask:
        {
            if(!attrPixelsRow)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, "Missing src alpha");
                return; 
            }

            for(int i=0;i<FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE;i++)
            {            
                uint8_t attr = *(attrPixelsRow++);
                uint8_t a = (attr & GPU_ATTR_PIXELWRITE0_MASK) ? 0xff : 0;

                // debug alpha
                dstRow[i] = gpu_col_from_rgbf(a/255.0f, 0, 0).value;  
            }        
            break;
        }      
        }// switch   
    } 
    else if(job->fb0->colorDepth == EColorDepth_8BPP)
    {
        uint16_t* dstRow = (uint16_t*)fb->pixelsData;
        uint8_t* srcPixelsRow = (uint8_t*)job->fb0->pixelsData;
        
        switch (cmd->blendMode)
        {
        case EBlendMode_None:       
        case EBlendMode_ColorKey:            
        case EBlendMode_Alpha:       
        {        
            // Get palette
            struct GpuBufferInfo* palBuffer = gpu_get_buffer_by_id(gpu, cmd->palBufferId);
            if(!palBuffer)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, ""); 
                return;
            }

            // Ensure valid palette format
            if(palBuffer->textureFormat != ETextureFormat_RGB16)
            {
                gpu_error(gpu, job, EGpuErrorCode_General, ""); 
                return;
            }

            for(int i=0;i<FRAME_W*VDP_TILEFRAME_BUFFER_Y_SIZE;i++)
            {
                uint8_t cid = *(srcPixelsRow++);      

                // lookup pal
                uint16_t c = ((uint16_t*)palBuffer->basePtr)[cid]; 

                dstRow[i] = c;
            }
            break;
        }
        }// switch   
    }
}


static inline void drawPixel(TileFrameBuffer_t* tile, int x, int y, uint16_t col)
{
    uint16_t* pixels = (uint16_t*)tile->pixelsData;

    y -= tile->y; // localise

    if( x < 0 || y < 0)
        return;
    if(x >= tile->w || y >= tile->h)
        return;

    uint32_t index = x + (y * tile->w);
    if(index >= tile->w*tile->h)
        return;
    pixels[ index ] = col;
    if(tile->attr)
        tile->attr[index] = GPU_ATTR_ALPHA_MASK;
}


void gpu_cmd_impl_DrawLine(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* tile)
{
    GPUCMD_DrawLine* cmd = (GPUCMD_DrawLine*)header;

    int x0 = cmd->x0;
    int y0 = cmd->y0;
    int x1 = cmd->x1;
    int y1 = cmd->y1;
    uint16_t col = cmd->col;

    int dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
    int dy = abs(y1-y0), sy = y0<y1 ? 1 : -1; 
    int err = (dx>dy ? dx : -dy)/2, e2;

    // Ref: https://rosettacode.org/wiki/Bitmap/Bresenham%27s_line_algorithm#C
    for(;;)
    {
        drawPixel(tile, x0, y0, col);

        if (x0==x1 && y0==y1) 
            break;

        e2 = err;

        if (e2 >-dx) 
        {
             err -= dy; x0 += sx; 
        }
        if (e2 < dy) 
        {
             err += dx; y0 += sy; 
        }
    } 
}


bool validate_gpu_cmd_impl_DrawLine(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_DrawLine));                
    return true;
}


bool toString_gpu_cmd_impl_DrawLine(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_DrawLine* cmd = (GPUCMD_DrawLine*)header;
    sprintf(buff, "x0:%d, y0:%d, x1:%d, y1:%d, col: %d", 
            cmd->x0, cmd->y0, 
            cmd->x1, cmd->y1,             
            cmd->col
        );
    return true;
}


void gpu_init_commands_3d(GpuState_t* state);
void gpu_init_commands(GpuState_t* state)
{   
    static bool lazyInit = false;
    if(!lazyInit)
    {
        initTilemap();
        lazyInit = 1;
    }

    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="ResetGpu", .id=EGPUCMD_ResetGpu, .cmd=gpu_cmd_impl_ResetGpu, .validator=validate_gpu_cmd_impl_ResetGpu, .toString=0});     
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="SetDebug", .id=EGPUCMD_SetDebug, .cmd=gpu_cmd_impl_SetDebug, .validator=validate_gpu_cmd_impl_SetDebug, .toString=0});           
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="FillRectCol", .id=EGPUCMD_FillRectCol, .cmd=gpu_cmd_impl_FillRectCol, .validator=validate_gpu_cmd_impl_FillRectCol, .toString=0});     
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="CreateBuffer", .id=EGPUCMD_CreateBuffer, .cmd=gpu_cmd_impl_CreateBuffer, .validator=validate_gpu_cmd_impl_CreateBuffer, .toString=toString_gpu_cmd_impl_CreateBuffer});
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="WriteBufferData", .id=EGPUCMD_WriteBufferData, .cmd=gpu_cmd_impl_WriteBufferData, .validator=validate_gpu_cmd_impl_WriteBufferData, .toString=toString_gpu_cmd_impl_WriteBufferData});
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="RegisterCmd", .id=EGPUCMD_RegisterCmd, .cmd=gpu_cmd_impl_RegisterCmd, .validator=validate_gpu_cmd_impl_RegisterCmd, .toString=0});
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="BlitRect", .id=EGPUCMD_BlitRect, .cmd=gpu_cmd_impl_BlitRect, .validator=validate_gpu_cmd_impl_BlitRect, .toString=toString_gpu_cmd_impl_BlitRect});         
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="DrawTileMap", .id=EGPUCMD_DrawTileMap, .cmd=gpu_cmd_impl_DrawTileMap, .validator=0, .toString=0});            
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="DrawWater", .id=EGPUCMD_DrawWater, .cmd=gpu_cmd_impl_DrawWater, .validator=0, .toString=0});            
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="CreateLinkedTilemapBuffer", .id=EGPUCMD_CreateLinkedTilemapBuffer, .cmd=gpu_cmd_impl_CreateLinkedTilemapBuffer, .validator=0, .toString=0});            
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="CompositeTile", .id=EGPUCMD_CompositeTile, .cmd=gpu_cmd_impl_CompositeTile, .validator=0, .toString=0});                
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="DrawLine", .id=EGPUCMD_DrawLine, .cmd=gpu_cmd_impl_DrawLine, .validator=validate_gpu_cmd_impl_DrawLine, .toString=toString_gpu_cmd_impl_DrawLine});

    // 3d extensions
    gpu_init_commands_3d(state);
    
}
