#include "scanline.h"
#include "picocom/display/display.h"
#include "picocom/display/gfx.h"
#include "lib/components/vdp1_core/vdp_client.h"
#include "picocom/utils/profiler.h"


// Globals
uint8_t* scanlineBlock;
uint32_t scanlineBlockSz = 0;
uint32_t currentLine = 0;
uint32_t currentSubLine = 0;
uint32_t currentBlock = 0;
uint32_t currentBaseLine = 0;



static int gfx_internal_create_upload_buffer_vdp1(const uint8_t* buffer, uint32_t sz, uint16_t bufferId, uint32_t memOffset, uint16_t w, uint16_t h, uint8_t textureFormat)
{
    Gfx_impl* g_Gfx = gfx_get_impl(); 
    struct VdpClientImpl_t* client = display_get_impl();
    if(!client || !g_Gfx)
        return SDKErr_Fail;


    vdp1_begin_frame(client, 1, 0);

    client->current_cmdFlags = 0;    // no default
    client->autoFlush_cmdFlag = 0;    

    struct GPUCMD_CreateBuffer* createCmd = (GPUCMD_CreateBuffer*)gfx_alloc_raw(sizeof(GPUCMD_CreateBuffer));
    if(!createCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(createCmd, EGPUCMD_CreateBuffer);        
    createCmd->bufferId = bufferId;
    createCmd->arena = EGPUBufferArena_Ram0;
    createCmd->memOffset = memOffset;
    createCmd->memSize = sz;
    createCmd->textureFormat = textureFormat;
    createCmd->w = w;
    createCmd->h = h;
    createCmd->header.cullTileMask = 0x1;

    if(buffer)
    {
        struct GPUCMD_WriteBufferData* writeCmd = (GPUCMD_WriteBufferData*)gfx_alloc_raw(sizeof(GPUCMD_WriteBufferData) + sz);
        if(!writeCmd)
            return SDKErr_Fail;
        GPU_INIT_CMD(writeCmd, EGPUCMD_WriteBufferData);        
        writeCmd->header.sz += sz; 
        writeCmd->offset = 0;
        writeCmd->bufferId = bufferId;
        writeCmd->flags = 0;
        writeCmd->header.cullTileMask = 0x1;

        // write
        writeCmd->dataSize = sz;
        for(int i=0;i<sz;i++)
            writeCmd->data[i] = buffer[i];    
    }
    
    return vdp1_end_frame( client, true, false );  
}


//
//
int scanline_calc_buffer_sz( bool paletteMode )
{    
    if(paletteMode)
    {
        return FRAME_W*FRAME_TILE_SZ_Y;        
    }
    else
    {
        return (FRAME_W*FRAME_TILE_SZ_Y) * sizeof(uint16_t) ;        
    }
}


int scanlinefeeder_init(struct ScanlineFeeder_t* feeder, bool paletteMode )
{
    int res;
    feeder->colorDepth = EColorDepth_BGR565;

    scanlineBlockSz = scanline_calc_buffer_sz( paletteMode );

    if(paletteMode)
    {        
        scanlineBlock = picocom_malloc(scanlineBlockSz);

        // create empty scanline buffer ( on vdp 1 )
        res = gfx_internal_create_upload_buffer_vdp1( 0, scanlineBlockSz, GFX_TEMP_SCANLINE_BUFFER_ID, 1024, FRAME_W, FRAME_TILE_SZ_Y, ETextureFormat_8BPP );        
    }
    else
    {        
        scanlineBlock = picocom_malloc(scanlineBlockSz);

        // create empty scanline buffer ( on vdp 1 )
        res = gfx_internal_create_upload_buffer_vdp1( 0, scanlineBlockSz, GFX_TEMP_SCANLINE_BUFFER_ID, 1024, FRAME_W, FRAME_TILE_SZ_Y, ETextureFormat_RGB16 );        
    }

    return res;
}


int scanlinefeeder_set_palette( struct ScanlineFeeder_t* feeder, const uint8_t* data, uint32_t sz)
{
    int res;
    // create pal on vdp2 ( final comp )
    gfx_set_vdp(EGfxTargetVDP2);
    res = display_upload_buffer(GFX_TEMP_SCANLINE_PAL_ID, EGfxTargetVDP2, data, sz, EGPUBufferArena_Ram0, 0, ETextureFormat_RGB16, 0, 0 );   
    gfx_set_vdp(EGfxTargetVDP1);
    return res;
}


int scanlinefeeder_write_scanline_8bpp( struct ScanlineFeeder_t* feeder, const uint8_t* buffer, uint32_t sz, uint32_t line )
{
    int res;
    struct VdpClientImpl_t* client = display_get_impl();
    if(!client)
        return SDKErr_Fail;

    if(currentLine != line)
    {
        // reset until resync
        currentLine = 0;    
        currentBlock = 0;
        currentSubLine = 0;
        currentBaseLine = 0;
        return 0;
    }

    memcpy(scanlineBlock + (currentSubLine * FRAME_W), buffer, sz);
    currentSubLine++;

    // block
    if(currentSubLine >= FRAME_TILE_SZ_Y)
    {                
        struct VdpPendingVDPCmd_t* cmdReq = client->currentPending;
        if(true)
        {
            //BEGIN_PROFILE();

            // blit tile only
            //uint32_t took = picocom_time_us_32() - lastFrameTime;
           // printf("took: %d us (%d ms)\n", took, (took)/1000); 
            //lastFrameTime = picocom_time_us_32();

            vdp1_begin_frame(client, gpu_calc_tile_cull_mask(currentBaseLine, FRAME_TILE_SZ_Y), 0);  
            
            // switch to 8bpp
            client->colorDepth = feeder->colorDepth;
            client->globalVdp2PalBufferId = GFX_TEMP_SCANLINE_PAL_ID;
            client->autoFlush_cmdFlag = 0;

            {
                const uint8_t* buffer = scanlineBlock;
                uint32_t sz = scanlineBlockSz;

                client->current_cmdFlags = 0;    // no default
                client->autoFlush_cmdFlag = 0;    

                struct GPUCMD_WriteBufferData* writeCmd = (GPUCMD_WriteBufferData*)gfx_alloc_raw(sizeof(GPUCMD_WriteBufferData) + sz);
                if(!writeCmd)
                    return SDKErr_Fail;
                GPU_INIT_CMD(writeCmd, EGPUCMD_WriteBufferData);        
                writeCmd->header.sz += sz; 
                writeCmd->offset = 0;
                writeCmd->bufferId = GFX_TEMP_SCANLINE_BUFFER_ID;
                writeCmd->flags = 0;
                writeCmd->header.cullTileMask = 0x1;
                writeCmd->allowNonTileZero = 1;

                // write
                writeCmd->dataSize = sz;
                for(int i=0;i<sz;i++)
                    writeCmd->data[i] = buffer[i];    
            }

            // flip last line
            if(currentBlock == 4) // TODO calc, hacked
            {
                client->current_cmdFlags |= EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay;   
            }

            gfx_draw_texture_blendmode_ex(GFX_TEMP_SCANLINE_BUFFER_ID, 0, currentBaseLine, 0, 0, 320, FRAME_TILE_SZ_Y,0, 0, 0, 0 );

            struct VdpPendingVDPCmd_t* cmdReq = client->currentPending;
            if(cmdReq)
            {        
                cmdReq->passId = currentBaseLine / FRAME_TILE_SZ_Y;
            }

            vdp1_end_frame( client, false, true );

            //END_PROFILE();
        }
        else
        {
            // underflow
        }

        currentBaseLine += FRAME_TILE_SZ_Y;
        currentSubLine = 0;
        currentBlock++;

        // reset
        if(currentBlock > 4)
        {
            currentLine = 0;    
            currentBlock = 0;
            currentSubLine = 0;
            currentBaseLine = 0;
        }
    }

    currentLine = line + 1;

    return SDKErr_OK;
}


uint32_t scanlinefeeder_calc_cmdlistallocaize( bool paletteMode )
{
    int blockSz = scanline_calc_buffer_sz( paletteMode );
    return blockSz +  sizeof(struct VDP1CMD_DrawCmdData) + sizeof(GPUCMD_WriteBufferData);
}


int scanlinefeeder_write_scanline_16bpp( struct ScanlineFeeder_t* feeder, const uint16_t* buffer, uint32_t bufferPixels, uint32_t line )
{
    int res;
    struct VdpClientImpl_t* client = display_get_impl();
    if(!client)
        return SDKErr_Fail;

    if(currentLine != line)
    {
        // reset until resync
        currentLine = 0;    
        currentBlock = 0;
        currentSubLine = 0;
        currentBaseLine = 0;
        return 0;
    }

    memcpy(scanlineBlock + (currentSubLine * FRAME_W * sizeof(uint16_t)), buffer, bufferPixels * sizeof(uint16_t));
    currentSubLine++;

    // block
    if(currentSubLine >= FRAME_TILE_SZ_Y)
    {                
        struct VdpPendingVDPCmd_t* cmdReq = client->currentPending;
        if(true)
        {
            //BEGIN_PROFILE();

            // blit tile only
            //uint32_t took = picocom_time_us_32() - lastFrameTime;
           // printf("took: %d us (%d ms)\n", took, (took)/1000); 
            //lastFrameTime = picocom_time_us_32();

            vdp1_begin_frame(client, gpu_calc_tile_cull_mask(currentBaseLine, FRAME_TILE_SZ_Y), 0);  
            
            // switch to 8bpp
            client->colorDepth = feeder->colorDepth;
            client->globalVdp2PalBufferId = 0;
            client->autoFlush_cmdFlag = 0;

            {
                const uint8_t* buffer = scanlineBlock;
                uint32_t sz = scanlineBlockSz;

                client->current_cmdFlags = 0;    // no default
                client->autoFlush_cmdFlag = 0;    

                struct GPUCMD_WriteBufferData* writeCmd = (GPUCMD_WriteBufferData*)gfx_alloc_raw(sizeof(GPUCMD_WriteBufferData) + sz);
                if(!writeCmd)
                    return SDKErr_Fail;
                GPU_INIT_CMD(writeCmd, EGPUCMD_WriteBufferData);        
                writeCmd->header.sz += sz; 
                writeCmd->offset = 0;
                writeCmd->bufferId = GFX_TEMP_SCANLINE_BUFFER_ID;
                writeCmd->flags = 0;
                writeCmd->header.cullTileMask = 0x1;
                writeCmd->allowNonTileZero = 1;

                // write
                writeCmd->dataSize = sz;
                for(int i=0;i<sz;i++)
                    writeCmd->data[i] = buffer[i];    
            }

            // flip last line
            if(currentBlock == 4) // TODO calc, hacked
            {
                client->current_cmdFlags |= EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay;   
            }

            gfx_draw_texture_blendmode_ex(GFX_TEMP_SCANLINE_BUFFER_ID, 0, currentBaseLine, 0, 0, 320, FRAME_TILE_SZ_Y,0, 0, 0, 0 );

            struct VdpPendingVDPCmd_t* cmdReq = client->currentPending;
            if(cmdReq)
            {        
                cmdReq->passId = currentBaseLine / FRAME_TILE_SZ_Y;
            }

            vdp1_end_frame( client, false, true );

            //END_PROFILE();
        }
        else
        {
            // underflow
        }

        currentBaseLine += FRAME_TILE_SZ_Y;
        currentSubLine = 0;
        currentBlock++;

        // reset
        if(currentBlock > 4)
        {
            currentLine = 0;    
            currentBlock = 0;
            currentSubLine = 0;
            currentBaseLine = 0;
        }
    }

    currentLine = line + 1;

    return SDKErr_OK;
}