#include "picocom/devkit.h"
#include "picocom/display/display.h"
#include "lib/components/vdp1_core/vdp_client.h"
#include "gfx.h"
#include "gpu/gpu.h"
#include "gpu/command_list.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// Globals
Gfx_impl* g_Gfx = 0;


//
//
uint16_t gfx_asset_pack_id(uint32_t resId)
{
    return (resId >> 16) & 0xffff;
}


uint16_t gfx_asset_res_id(uint32_t resId)
{
    return (resId >> 0) & 0xffff;
}


//
//
Gfx_impl* gfx_get_impl()
{
    return g_Gfx;
}


int gfx_init()
{
    if(g_Gfx)
        return SDKErr_OK;

    // Alloc state
    size_t sss = sizeof(Gfx_impl);
    g_Gfx = (Gfx_impl*)picocom_malloc(sizeof(Gfx_impl));
    if(!g_Gfx)
        return SDKErr_Fail;
    memset(g_Gfx, 0, sizeof(Gfx_impl));        
    
    // init base buffer id (0 reserved for temp)
    for(int i=0;i<2;i++) // iter over vdp1,2 etc
    {
        g_Gfx->allocPools[i].bufferId = GFX_TEMP_BUFFER_BASE;  // init buffer id
        g_Gfx->allocPools[i].arenas[EGPUBufferArena_Ram0].offset = GFX_TEMP_BUFFER_SZ;   
    }

    struct VdpClientImpl_t* client = display_get_impl();        
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    int res = vdp1_begin_frame(client, 0xffff, 0);
    if(res != SDKErr_OK)
        return res;
    g_Gfx->inFrame = 1;

    return SDKErr_OK;
}


void gfx_deinit()
{
    if(!g_Gfx)
        return;

    picocom_free(g_Gfx);
    g_Gfx = 0;
}


int gfx_mount_asset_pack(const struct GfxAssetPack* assetPack)
{
    if(!g_Gfx)
        return SDKErr_OK;

    if(assetPack->packId >= MAX_ASSET_PACKID)
        return SDKErr_Fail;

    g_Gfx->assetPacks[assetPack->packId] = assetPack;

    return SDKErr_OK;
}


const struct GfxResourceInfo* gfx_get_resource_info(uint32_t resId)
{
    if(!g_Gfx)
        return 0;

    uint16_t packId = gfx_asset_pack_id(resId);
    if(packId >= MAX_ASSET_PACKID)
        return 0;

    const struct GfxAssetPack* pack = g_Gfx->assetPacks[packId];
    if(!pack)
        return 0;

    uint16_t index = gfx_asset_res_id(resId);
    if(index >= pack->assetCount)
        return 0;

    return (GfxResourceInfo*)(pack->basePtr + pack->assetOffsets[index]);
}


uint32_t gfx_get_resource_offset(uint32_t resId)
{
    if(!g_Gfx)
        return 0;

    uint16_t packId = gfx_asset_pack_id(resId);
    if(packId >= MAX_ASSET_PACKID)
        return 0;

    const struct GfxAssetPack* pack = g_Gfx->assetPacks[packId];
    if(!pack)
        return 0;

    uint16_t index = gfx_asset_res_id(resId);
    if(index >= pack->assetCount)
        return 0;

    return pack->assetOffsets[index];
}


const uint8_t* gfx_get_raw_resources(uint32_t resId)
{
    if(!g_Gfx)
        return 0;

    uint16_t packId = gfx_asset_pack_id(resId);
    if(packId >= MAX_ASSET_PACKID)
        return 0;

    const struct GfxAssetPack* pack = g_Gfx->assetPacks[packId];
    if(!pack)
        return 0;

    uint16_t index = gfx_asset_res_id(resId);
    if(index >= pack->assetCount)
        return 0;

    uint32_t offset = pack->assetOffsets[index];

    return (uint8_t*)(pack->basePtr + pack->assetOffsets[index] + sizeof(GfxResourceInfo));
}


uint32_t gfx_get_raw_resource_size(uint32_t resId)
{
    const struct GfxResourceInfo* res = gfx_get_resource_info(resId);
    if(!res)
        return 0;
    return res->size - sizeof(GfxResourceInfo);
}


const struct GfxResourceInfo* gfx_get_resource_info_of_type(uint32_t resId, uint8_t type)
{
    const struct GfxResourceInfo* res = gfx_get_resource_info(resId);
    if(!res || res->type != type)
        return 0;
    return res;
}


uint32_t gfx_upload_resource_flash(uint32_t resourceId)
{
    return gfx_upload_resource(resourceId, g_Gfx->currentVdpId, EGPUBufferArena_Flash0);
}


uint32_t gfx_upload_resource_ram(uint32_t resourceId)
{
    return gfx_upload_resource(resourceId, g_Gfx->currentVdpId, EGPUBufferArena_Ram0);
}


uint32_t gfx_upload_resource(uint32_t resourceId, uint8_t vdpId, uint8_t arenaId)
{    
    return gfx_upload_resource_impl(resourceId, vdpId, arenaId, 0);
}


uint32_t gfx_upload_resource_impl(uint32_t resourceId, uint8_t vdpId, uint8_t arenaId, uint32_t* palBufferIdOut)
{
    int res;

    struct VdpClientImpl_t* impl = display_get_impl();	
    if(!impl)
        return 0;

    const struct GfxResourceInfo* gfxRes = (struct GfxResourceInfo*)gfx_get_resource_info(resourceId);
    if(!gfxRes)
        return 0;

    // Handle type
    switch (gfxRes->type) 
    {
        case EGfxAssetType_Texture:
        {
            const struct GfxTextureInfo* tex = (struct GfxTextureInfo*)gfxRes;

            // upload pal
            if(tex->format == ETextureFormat_8BPP)
            {
                uint32_t bufferId;
                uint32_t offset;
                if(!gfx_alloc_block(tex->dataSize, vdpId, arenaId, &bufferId, &offset))
                    return 0;
                if(bufferId == -1)
                    return -1;
                                    
                // Get pal assets
                if(tex->palAssetId)
                {
                    const struct GfxTextureInfo* palRes = (struct GfxTextureInfo*)gfx_get_resource_info(tex->palAssetId);
                    if(!palRes)
                    {
                        picocom_panic(SDKErr_Fail, "Missing palette asset");
                        return 0;
                    }  
                    
                    uint32_t palBufferId;
                    uint32_t palOffset;
                    if(!gfx_alloc_block(palRes->dataSize, vdpId, arenaId, &palBufferId, &palOffset))
                        return 0;
                    if(bufferId == -1)
                        return -1;                             

                    // ensure contiguous as return values only returns single id
                    assert(bufferId + 1 == palBufferId);                                       

                    res = display_upload_buffer(palBufferId, vdpId, palRes->data, palRes->dataSize, arenaId, offset, palRes->format, palRes->w, palRes->h );   
                    if(res != SDKErr_OK)
                        return 0;

                    // Output pal buffer
                    if(palBufferIdOut)    
                        *palBufferIdOut = palBufferId;
                }
                                        
                res = display_upload_buffer(bufferId, vdpId, tex->data, tex->dataSize, arenaId, offset, tex->format, tex->w, tex->h );   
                if(res != SDKErr_OK)
                    return 0;

                return bufferId;
                    
            }
            else if(tex->format == ETextureFormat_RGBA16 || tex->format == ETextureFormat_RGB16)
            {        
                uint32_t bufferId;
                uint32_t offset;
                if(!gfx_alloc_block(tex->dataSize, vdpId, arenaId, &bufferId, &offset))
                    return 0;
                if(bufferId == -1)
                    return -1;
                                    
                res = display_upload_buffer(bufferId, vdpId, tex->data, tex->dataSize, arenaId, offset, tex->format, tex->w, tex->h );   
                if(res != SDKErr_OK)
                    return 0;

                return bufferId;
            }
            else
            {
                return 0;
            }
        }
        case EGfxAssetType_Mesh:
        {
            const struct MeshInfo* mesh = (struct MeshInfo*)gfxRes;
            
            
            // Get vertex ptr
            float* vertices = (float*)&mesh->data[mesh->verticesOffset];
            float* texcoords = (float*)&mesh->data[mesh->texCoordOffset];
            float* normals = (float*)&mesh->data[mesh->normalsOffset];
            uint16_t* faces = (uint16_t*)&mesh->data[mesh->facesOffset];

            struct GpuMeshMaterial3D mat = {
                .r=0.85f, .g=0.55f, .b=0.25f,
                .ambientStrength=0.2f, 
                .diffuseStrength=0.7f,
                .specularStrength=0.8f,
                .specularExponent=64
            };         
               
            // Upload mesh to vdp
            uint32_t meshBufferId = gfx_upload_mesh3d( vdpId, arenaId, 
                vertices, mesh->verticesCount,
                texcoords, mesh->texCoordCount,
                normals, mesh->normalsCount,
                faces,         
                mesh->nb_faces, 
                mesh->len_face,
                mesh->bounds,                
                &mat );

            return meshBufferId;            
        }
        default:
            return 0;
    }

    return 0;
}


uint32_t gfx_upload_buffer( uint8_t vdpId, uint8_t arenaId, const uint8_t* data, uint32_t dataSize, uint8_t format, uint16_t w, uint16_t h )
{    
    uint32_t bufferId;
    uint32_t offset;
    if(!gfx_alloc_block(dataSize, vdpId, arenaId, &bufferId, &offset))
        return 0;

    if(bufferId == -1)
        return -1;

    int res = display_upload_buffer(bufferId, vdpId, data, dataSize, arenaId, offset, format, w, h );   
        
    if(res != SDKErr_OK)
        return -1;

    return bufferId;
}


int gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t col, uint8_t a)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;
        
    struct GPUCMD_FillRectCol* fillCmd = (GPUCMD_FillRectCol*)gfx_alloc_raw(sizeof(GPUCMD_FillRectCol));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_FillRectCol);
    fillCmd->col = col;
    fillCmd->a = a;
    fillCmd->x = x;
    fillCmd->y = y;
    fillCmd->w = w;
    fillCmd->h = h;
    fillCmd->header.cullTileMask = gpu_calc_tile_cull_mask(y, h);
    fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;
    fillCmd->blendMode = EBlendMode_None;

    return SDKErr_OK;
}


int gfx_fill_rect_blended(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t col, uint8_t a, uint8_t blendMode)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;
        
    struct GPUCMD_FillRectCol* fillCmd = (GPUCMD_FillRectCol*)gfx_alloc_raw(sizeof(GPUCMD_FillRectCol));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_FillRectCol);
    fillCmd->col = col;
    fillCmd->a = a;
    fillCmd->x = x;
    fillCmd->y = y;
    fillCmd->w = w;
    fillCmd->h = h;
    fillCmd->header.cullTileMask = gpu_calc_tile_cull_mask(y, h);
    fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;
    fillCmd->blendMode = blendMode;

    return SDKErr_OK;
}

const struct GfxGlyphInfo* font_lookup_codepoint(const struct GfxFontInfo* font, uint16_t c)
{
    if(!font)
        return 0;

    for(int i=0;i<font->glyphCnt;i++)
    {
        if(font->glyphs[i].c == c)
            return &font->glyphs[i];
    }

    return 0;
}


int gfx_internal_temp_buffer(uint8_t groupId, const uint8_t* buffer, uint32_t sz, uint8_t textureFormat, uint16_t w, uint16_t h, uint16_t cullTileMask)
{
    if(sz > GFX_TEMP_BUFFER_SZ)
        return SDKErr_Fail;

    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_CreateBuffer* createCmd = (GPUCMD_CreateBuffer*)gfx_alloc_raw(sizeof(GPUCMD_CreateBuffer));
    if(!createCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(createCmd, EGPUCMD_CreateBuffer);        
    createCmd->bufferId = GFX_TEMP_BUFFER_ID;
    createCmd->arena = EGPUBufferArena_Ram0;
    createCmd->memOffset = 0;
    createCmd->memSize = sz;
    createCmd->textureFormat = textureFormat;
    createCmd->w = w;
    createCmd->h = h;
    createCmd->header.cullTileMask = cullTileMask; // tile X
    
    struct GPUCMD_WriteBufferData* writeCmd = (GPUCMD_WriteBufferData*)gfx_alloc_raw(sizeof(GPUCMD_WriteBufferData) + sz);
    if(!writeCmd)
        return SDKErr_Fail;
    GPU_INIT_CMD(writeCmd, EGPUCMD_WriteBufferData);        
    writeCmd->header.sz += sz; 
    writeCmd->offset = 0;
    writeCmd->bufferId = GFX_TEMP_BUFFER_ID;
    writeCmd->flags = 0;
    writeCmd->header.cullTileMask = 0; // no cull to allow tile sharing

    // write
    writeCmd->dataSize = sz;
    for(int i=0;i<sz;i++)
        writeCmd->data[i] = buffer[i];    

    return SDKErr_OK;
}


int gfx_internal_get_temp_buffer_id()
{
    return GFX_TEMP_BUFFER_ID;
}


int gfx_internal_get_temp_buffer_size()
{
    return GFX_TEMP_BUFFER_SZ;
}


bool gfx_alloc_block(uint32_t sz, uint8_t vdpId, uint8_t arenaId, uint32_t* bufferIdOut, uint32_t* memBaseOffsetOut)
{
    if(!g_Gfx || vdpId >= 2 || arenaId >= 2 || !sz)
        return 0;

    GfxVDPAllocPool* pool = &g_Gfx->allocPools[vdpId];   
    GfxAllocPool* alloc = &pool->arenas[arenaId];

    // next buffer id
    uint32_t bufferId = pool->bufferId;
    uint32_t offset = 0;

    if(arenaId == EGPUBufferArena_Flash0)
    {        
        uint32_t pageCount = CEIL_INT(sz, GPU_FLASH_BUFFER_PAGE_SIZE);
        if(pageCount <= 0)
            pageCount = 1;
        
        offset = alloc->offset;
        alloc->offset += ( pageCount*GPU_FLASH_BUFFER_PAGE_SIZE);
    } 
    else
    {
        // unaligned bump alloc
        offset = alloc->offset;
        alloc->offset += sz;
    }

    if(alloc->offset >= GPU_FLASH_STORAGE_SIZE)
    {
        picocom_panic(SDKErr_Fail, "Max gfx alloc");
    }

    pool->bufferId++;    

    *bufferIdOut = bufferId;
    *memBaseOffsetOut = offset;

    return 1;
}


bool gfx_alloc_buffer_id(uint8_t vdpId, uint8_t arenaId, uint32_t* bufferIdOut)
{
    if(!g_Gfx || vdpId >= 2 || arenaId >= 2)
        return 0;

    GfxVDPAllocPool* pool = &g_Gfx->allocPools[vdpId];   
    GfxAllocPool* alloc = &pool->arenas[arenaId];

    // next buffer id
    uint32_t bufferId = pool->bufferId;

    pool->bufferId++;    

    *bufferIdOut = bufferId;

    return 1;
}


int gfx_set_palette(uint16_t* cols, uint16_t cnt)
{   
    uint16_t cull = 0; // set on all tiles
    return gfx_internal_temp_buffer(g_Gfx->currentVdpId, (const uint8_t*)cols, cnt*sizeof(uint16_t), ETextureFormat_RGB16, 0, 0, cull );
}


int gfx_draw_text(uint32_t fontResourceId, uint32_t fontTextureBufferId, int16_t x, int16_t y, const char* text, uint16_t col)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    // Get font res
    const struct GfxFontInfo* font = (struct GfxFontInfo*)gfx_get_resource_info_of_type(fontResourceId, EGfxAssetType_FontInfo);
    if(!font)
        return SDKErr_OK;

    const struct GfxTextureInfo* tex = (struct GfxTextureInfo*)gfx_get_resource_info_of_type(font->fontTextureAssetId, EGfxAssetType_Texture);
    if(!tex)
        return SDKErr_OK;

    float curx = x;
    float cury = y;
    const char* curchar = text;

    int itercnt = 0;
    while (*curchar)
    {
        itercnt++;
        char c = *curchar;
        
        if (c == '\n')
        {
            cury += font->lineHeight;
            curx = x;
            curchar++;
            continue;
        }

        if(c == ' ')
        {
            curx += font->spaceWidth;
            curchar++;
            continue;
        }
        else if(c == '\t')
        {
            curx += (font->spaceWidth*4);
            curchar++;
            continue;
        }

        const struct GfxGlyphInfo* g = font_lookup_codepoint(font, c);
        if(!g)
        {
            curx += font->spaceWidth;
            curchar++;
            continue;
        }

        struct GPUCMD_BlitRect* fillCmd = (GPUCMD_BlitRect*)gfx_alloc_raw(sizeof(GPUCMD_BlitRect));
        if(!fillCmd)
            break;

        GPU_INIT_CMD(fillCmd, EGPUCMD_BlitRect);
        fillCmd->bufferId = fontTextureBufferId;
        fillCmd->dstX = curx + g->offsetX;
        fillCmd->dstY = cury + g->offsetY;
        fillCmd->srcX = g->x;
        fillCmd->srcY = g->y;      
        fillCmd->w = g->w;
        fillCmd->h = g->h;
        fillCmd->colKey = 0;    // transparent        
        fillCmd->palBufferId = col; 
        fillCmd->a = GPU_ATTR_ALPHA_MASK;
        fillCmd->writeAlpha = GPU_ATTR_ALPHA_MASK;
        fillCmd->blendMode = EBlendMode_FillMasked;

        curx += g->advance;
        curchar++;
    }

    return SDKErr_OK;
}


int gfx_draw_text_ex(uint32_t fontResourceId, uint32_t fontTextureBufferId, int16_t x, int16_t y, const char* text, uint16_t colKey, uint8_t flags)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    // Get font res
    const struct GfxFontInfo* font = (struct GfxFontInfo*)gfx_get_resource_info_of_type(fontResourceId, EGfxAssetType_FontInfo);
    if(!font)
        return SDKErr_OK;

    const struct GfxTextureInfo* tex = (struct GfxTextureInfo*)gfx_get_resource_info_of_type(font->fontTextureAssetId, EGfxAssetType_Texture);
    if(!tex)
        return SDKErr_OK;

    float curx = x;
    float cury = y;
    const char* curchar = text;

    int itercnt = 0;
    while (*curchar)
    {
        itercnt++;
        char c = *curchar;
        
        if (c == '\n')
        {
            cury += font->lineHeight;
            curx = x;
            curchar++;
            continue;
        }

        if(c == ' ')
        {
            curx += font->spaceWidth;
            curchar++;
            continue;
        }
        else if(c == '\t')
        {
            curx += (font->spaceWidth*4);
            curchar++;
            continue;
        }

        const struct GfxGlyphInfo* g = font_lookup_codepoint(font, c);
        if(!g)
        {
            curx += font->spaceWidth;
            curchar++;
            continue;
        }

        gfx_draw_texture( fontTextureBufferId, curx + g->offsetX, cury + g->offsetY, g->x, g->y, g->w, g->h, colKey, flags); 

        curx += g->advance;
        curchar++;
    }

    return SDKErr_OK;
}


int gfx_get_text_size(uint32_t fontResourceId, uint32_t fontTextureBufferId, const char* text, uint16_t* wOut, uint16_t* hOut)
{
    if(!text)
        return SDKErr_Fail;

    // Get font res
    const struct GfxFontInfo* font = (struct GfxFontInfo*)gfx_get_resource_info_of_type(fontResourceId, EGfxAssetType_FontInfo);
    if(!font)
        return SDKErr_Fail;

    float curx = 0;
    float cury = 0;
    const char* curchar = text;

    int itercnt = 0;
    while (*curchar)
    {
        itercnt++;
        char c = *curchar;
        
        if (c == '\n')
        {
            cury += font->lineHeight;
            curx = 0;
            curchar++;
            continue;
        }

        if(c == ' ')
        {
            curx += font->spaceWidth;
            curchar++;
            continue;
        }
        else if(c == '\t')
        {
            curx += (font->spaceWidth*4);
            curchar++;
            continue;
        }

        const struct GfxGlyphInfo* g = font_lookup_codepoint(font, c);
        if(!g)
        {
            curx += font->spaceWidth;
            curchar++;
            continue;
        }

        *wOut = curx + g->offsetX + g->w;
        *hOut = cury + g->offsetY + g->h;

        curx += g->advance;
        curchar++;
    }

    return SDKErr_OK;
}


int gfx_draw_texture(uint32_t texturebufferId, int16_t x, int16_t y, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH, uint16_t colKey, uint8_t flags)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_BlitRect* fillCmd = (GPUCMD_BlitRect*)gfx_alloc_raw(sizeof(GPUCMD_BlitRect));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_BlitRect);
    fillCmd->bufferId = texturebufferId;
    fillCmd->dstX = x;
    fillCmd->dstY = y;
    fillCmd->srcX = srcX;
    fillCmd->srcY = srcY;
    fillCmd->w = srcW;
    fillCmd->h = srcH;
    fillCmd->colKey = colKey;    // transparent
    fillCmd->palBufferId = 0;   // TODO: get from texture res info
    fillCmd->flags = flags;
    fillCmd->a = GPU_ATTR_ALPHA_MASK;
    fillCmd->writeAlpha = GPU_ATTR_ALPHA_MASK;
    fillCmd->blendMode = EBlendMode_ColorKey;

    // enable tile culling
    fillCmd->header.cullTileMask = gpu_calc_tile_cull_mask(fillCmd->dstY, fillCmd->h);    
    fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;

    return SDKErr_OK;    
}


int gfx_draw_texture_blendmode(uint32_t texturebufferId, int16_t x, int16_t y, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH, uint16_t colKeyOrPal, uint8_t flags, uint8_t blendMode)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_BlitRect* fillCmd = (GPUCMD_BlitRect*)gfx_alloc_raw(sizeof(GPUCMD_BlitRect));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_BlitRect);
    fillCmd->bufferId = texturebufferId;
    fillCmd->dstX = x;
    fillCmd->dstY = y;
    fillCmd->srcX = srcX;
    fillCmd->srcY = srcY;
    fillCmd->w = srcW;
    fillCmd->h = srcH;
    fillCmd->colKey = colKeyOrPal;    // transparent
    fillCmd->palBufferId = 0;   // TODO: get from texture res info
    fillCmd->flags = flags;
    fillCmd->a = GPU_ATTR_ALPHA_MASK;
    fillCmd->writeAlpha = GPU_ATTR_ALPHA_MASK;
    fillCmd->blendMode = blendMode;

    // enable tile culling
    fillCmd->header.cullTileMask = gpu_calc_tile_cull_mask(fillCmd->dstY, fillCmd->h);    
    fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;

    return SDKErr_OK;  
}



int gfx_draw_texture_blendmode_ex(uint32_t texturebufferId, int16_t x, int16_t y, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH, uint16_t colKeyOrPal, uint8_t flags, uint8_t blendMode, uint16_t palBufferId)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_BlitRect* fillCmd = (GPUCMD_BlitRect*)gfx_alloc_raw(sizeof(GPUCMD_BlitRect));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_BlitRect);
    fillCmd->bufferId = texturebufferId;
    fillCmd->dstX = x;
    fillCmd->dstY = y;
    fillCmd->srcX = srcX;
    fillCmd->srcY = srcY;
    fillCmd->w = srcW;
    fillCmd->h = srcH;
    fillCmd->colKey = colKeyOrPal;    // transparent
    fillCmd->palBufferId = palBufferId;   // TODO: get from texture res info
    fillCmd->flags = flags;
    fillCmd->a = GPU_ATTR_ALPHA_MASK;
    fillCmd->writeAlpha = GPU_ATTR_ALPHA_MASK;
    fillCmd->blendMode = blendMode;

    // enable tile culling
    fillCmd->header.cullTileMask = gpu_calc_tile_cull_mask(fillCmd->dstY, fillCmd->h);    
    fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;

    return SDKErr_OK;  
}


int gfx_draw_tilemap(uint32_t tilemapDataBufferId, uint32_t tilemapStateBufferId, uint32_t tilemapAttribBufferId, int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t tileIdMask, uint8_t tileGroupMask, float time, uint32_t seed)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_DrawTileMap* fillCmd = (GPUCMD_DrawTileMap*)gfx_alloc_raw(sizeof(GPUCMD_DrawTileMap));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_DrawTileMap);
    fillCmd->tilemapDataBufferId = tilemapDataBufferId;
    fillCmd->tilemapStateBufferId = tilemapStateBufferId;
    fillCmd->tilemapAttribBufferId = tilemapAttribBufferId;
    fillCmd->x = x;
    fillCmd->y = y;    
    fillCmd->w = w;
    fillCmd->h = h;    
    fillCmd->tileIdMask = tileIdMask;
    fillCmd->tileGroupMask = tileGroupMask;
    fillCmd->time = time;
    fillCmd->seed = seed;

    // enable tile culling
    fillCmd->header.cullTileMask = gpu_calc_tile_cull_mask(fillCmd->y, fillCmd->h);    
    fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;

    return SDKErr_OK;
}


int gfx_draw_water(uint32_t tilemapDataBufferId, uint32_t tilemapStateBufferId, uint32_t tilemapAttribBufferId, int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t tileIdMask, uint8_t tileGroupMask, float time, uint32_t seed)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_DrawTileMap* fillCmd = (GPUCMD_DrawTileMap*)gfx_alloc_raw(sizeof(GPUCMD_DrawTileMap));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_DrawWater);
    fillCmd->tilemapDataBufferId = tilemapDataBufferId;
    fillCmd->tilemapStateBufferId = tilemapStateBufferId;
    fillCmd->tilemapAttribBufferId = tilemapAttribBufferId;
    fillCmd->x = x;
    fillCmd->y = y;    
    fillCmd->w = w;
    fillCmd->h = h;    
    fillCmd->tileIdMask = tileIdMask;
    fillCmd->tileGroupMask = tileGroupMask;
    fillCmd->time = time;
    fillCmd->seed = seed;
    fillCmd->blendMode = EBlendMode_Alpha;

    // enable tile culling
    fillCmd->header.cullTileMask = gpu_calc_tile_cull_mask(fillCmd->y, fillCmd->h);    
    fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;

    return SDKErr_OK;
}


int gfx_link_tilemap(uint16_t tilemapDataBufferId, uint16_t tilemapStateBufferId, uint16_t tilemapDataBufferIds[9], uint16_t tilemapStateBufferIds[9])
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_CreateLinkedTilemapBuffer* fillCmd = (GPUCMD_CreateLinkedTilemapBuffer*)gfx_alloc_raw(sizeof(GPUCMD_CreateLinkedTilemapBuffer));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_CreateLinkedTilemapBuffer);    
    fillCmd->tilemapDataBufferId = tilemapDataBufferId;              
    fillCmd->tilemapStateBufferId = tilemapStateBufferId;            
    memcpy(fillCmd->edgeTilemapDataBufferIds, tilemapDataBufferIds, sizeof(uint16_t[9])); 
    memcpy(fillCmd->edgeTilemapStateBufferIds, tilemapStateBufferIds, sizeof(uint16_t[9]));

    // enable tile culling
    fillCmd->header.cullTileMask = 0b1;
    fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;

    return SDKErr_OK;
}


int gfx_begin_frame()
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;        
    g_Gfx->triCnt = 0;
    g_Gfx->currentVdpId = EGfxTargetVDP1;
    
    int res = vdp1_begin_frame(client, 0xffff, 0);
    if(res != SDKErr_OK)
        return res;
    g_Gfx->inFrame = 1;
    
    return display_begin_frame();
}


int gfx_end_frame()
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    g_Gfx->inFrame = 0;

    client->current_cmdFlags |= EVDP1CMD_DrawCmdData_completeFlags_FlipDisplay;

    vdp1_end_frame(client, false, true); // service update

    return display_end_frame(); // send display cmds
}


int gfx_debug_set_gpu_debug(uint8_t dumpBufferUploads)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    int res = vdp1_begin_frame(client, 0xffff, 0);
    if(res != SDKErr_OK)
        return res;
    g_Gfx->inFrame = 1;

    struct GPUCMD_SetDebug* cmd = (GPUCMD_SetDebug*)gfx_alloc_raw(sizeof(GPUCMD_SetDebug));
    if(!cmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(cmd, EGPUCMD_SetDebug);
    cmd->dumpBufferUploads = dumpBufferUploads;  
    cmd->header.cullTileMask = 1 << 0;

    vdp1_end_frame(client, true, false); // service update

    return SDKErr_OK;
}



int gfx_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t col)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_DrawLine* fillCmd = (GPUCMD_DrawLine*)gfx_alloc_raw(sizeof(GPUCMD_DrawLine));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_DrawLine);
    fillCmd->x0 = x0;
    fillCmd->y0 = y0;
    fillCmd->x1 = x1;
    fillCmd->y1 = y1;
    fillCmd->col = col;
    
    // enable tile culling
    fillCmd->header.cullTileMask = 0; // TODO gpu_calc_tile_cull_mask(fillCmd->dstY, fillCmd->h);    
    //fillCmd->header.flags |= EGpuCmd_Header_Flags_TileCullMask;

    return SDKErr_OK;  
}


int gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t col)
{    
    if(gfx_draw_line(x, y, x + w, y, col) != SDKErr_OK)
        return SDKErr_Fail;
    if(gfx_draw_line(x + w, y, x + w, y + h, col) != SDKErr_OK)
        return SDKErr_Fail;    
    if(gfx_draw_line(x, y + h, x + w, y + h, col) != SDKErr_OK)
        return SDKErr_Fail;    
    if(gfx_draw_line(x, y, x, y + h, col) != SDKErr_OK)
        return SDKErr_Fail;        
    return SDKErr_OK;
}


int gfx_draw_triangle_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t col)
{
    gfx_draw_line(x0, y0, x1, y1, col);
    gfx_draw_line(x1, y1, x2, y2, col);
    gfx_draw_line(x2, y2, x0, y0, col);
    return SDKErr_OK; // no err checks
}


int gfx_composite(uint8_t blendMode)
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;
        
    struct GPUCMD_CompositeTile* fillCmd = (GPUCMD_CompositeTile*)gfx_alloc_raw(sizeof(GPUCMD_CompositeTile));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_CompositeTile);
    fillCmd->blendMode = blendMode;

    return SDKErr_OK;
}


struct GpuCmd_Header* gfx_alloc_raw(uint32_t sz)
{
    struct VdpClientImpl_t* client = display_get_impl();        
    if(!g_Gfx || !client)
        return 0;

    return vdp1_cmd_add_next(client, g_Gfx->currentVdpId, sz);
}


int gfx_set_vdp(enum EGfxTargetVDP vdpId)
{
    struct VdpClientImpl_t* client = display_get_impl();        
    if(!g_Gfx || !client)
        return SDKErr_Fail;
    g_Gfx->currentVdpId = vdpId;

    return SDKErr_OK;
}


int gfx_upload_asset_pack(const struct GfxAssetPack* assetPack, struct GfxAssetPackUpload* upload, uint8_t arenaId)
{       
    int res;    
    if(!upload)
        return SDKErr_Fail;

    // upload blocks to vdp
    if( assetPack->compressed )
    {
        // alloc flash blocks      
        if(!gfx_alloc_block(assetPack->uncompressedSize, gfx_get_impl()->currentVdpId, arenaId, &upload->baseBufferId, &upload->baseAddress))
            return SDKErr_Fail;        

        if( assetPack->isInPlace )
            res = SDKErr_OK;
        else
            res = display_upload_buffer_compressed(upload->baseBufferId, gfx_get_impl()->currentVdpId, assetPack->compressedBasePtr, assetPack->uncompressedSize, assetPack->compressedSize, arenaId, upload->baseAddress, 0, 0, 0 );           
    }
    else
    {
        // alloc flash blocks      
        if(!gfx_alloc_block(assetPack->size, gfx_get_impl()->currentVdpId, arenaId, &upload->baseBufferId, &upload->baseAddress))
            return SDKErr_Fail;        

        res = display_upload_buffer(upload->baseBufferId, gfx_get_impl()->currentVdpId, assetPack->basePtr, assetPack->size, arenaId, upload->baseAddress, 0, 0, 0 );   
    }
    if(res != SDKErr_OK)
        return res;

    upload->pack = assetPack;
    upload->vdpId = gfx_get_impl()->currentVdpId;
    upload->arenaId = arenaId;

    return SDKErr_OK;
}


int gfx_upload_buffer_pack( struct GfxAssetPackUpload* upload, uint32_t resourceId )
{
    if(!upload || !upload->pack)
        return SDKErr_Fail;
    
    if(gfx_get_impl()->currentVdpId != upload->vdpId)
    {
        printf("upload->vdpId != upload->vdpId\n"); // NOTE: could remove this but generally good practice to bind vdp first
        return SDKErr_Fail;
    }

    const struct GfxTextureInfo* tex = (struct GfxTextureInfo*)gfx_get_resource_info_of_type( resourceId, EGfxAssetType_Texture );
    if(!tex)
        return SDKErr_Fail;
    
    // Alloc virtual buffer id                
    uint32_t bufferId;
    if(!gfx_alloc_buffer_id (upload->vdpId, upload->arenaId, &bufferId ))
        return 0;

    // Compressed have diff layout, tex info has offset in compresstion block
    if(upload->pack->compressed)
    {
        uint32_t memOffset = upload->baseAddress + tex->dataOffset;
        int res = display_create_buffer( bufferId, upload->vdpId, tex->dataSize, upload->arenaId, memOffset, tex->format, tex->w, tex->h, upload->baseBufferId );      
        if(res < SDKErr_OK)
            return res;
    }
    else
    {
        uint32_t memOffset = upload->baseAddress + gfx_get_resource_offset(resourceId) + sizeof(struct GfxTextureInfo);
        int res = display_create_buffer( bufferId, upload->vdpId, tex->dataSize, upload->arenaId, memOffset, tex->format, tex->w, tex->h, upload->baseBufferId );      
        if(res < SDKErr_OK)
            return res;
    }
    
    return bufferId;
}


int gfx_init_renderer3d()
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    int res;

    res = vdp1_begin_frame(client, 1, 0);
    if(res != SDKErr_OK)
        return res;

    struct GPUCMD_InitRenderer3D* fillCmd = (GPUCMD_InitRenderer3D*)gfx_alloc_raw(sizeof(GPUCMD_InitRenderer3D));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_InitRenderer3D);
    
    // enable tile culling
    fillCmd->header.cullTileMask = 0;

    res = vdp1_end_frame(client, true, false);
    if(res != SDKErr_OK)
        return res;

    return SDKErr_OK;
}


int gfx_set_shader3d( uint32_t shaderId )
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_SetShader3D* fillCmd = (GPUCMD_SetShader3D*)gfx_alloc_raw(sizeof(GPUCMD_SetShader3D));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_SetShader3D);
    fillCmd->shaderId = shaderId;
    
    // enable tile culling
    fillCmd->header.cullTileMask = 0;

    return SDKErr_OK;
}


int gfx_set_model_matrix3d( float* M )
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_SetMatrix3D* fillCmd = (GPUCMD_SetMatrix3D*)gfx_alloc_raw(sizeof(GPUCMD_SetMatrix3D));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_SetMatrix3D);
    memcpy(fillCmd->M, M, sizeof(fillCmd->M) );
    
    // enable tile culling
    fillCmd->header.cullTileMask = 0;

    return SDKErr_OK;
}


int gfx_draw_mesh3d( uint32_t meshBufferId, uint32_t textureBufferId, bool culling )
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_DrawMesh3D* fillCmd = (GPUCMD_DrawMesh3D*)gfx_alloc_raw(sizeof(GPUCMD_DrawMesh3D));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_DrawMesh3D);
    fillCmd->meshBufferId = meshBufferId;
    fillCmd->textureBufferId = textureBufferId;
    fillCmd->culling = culling;
    
    // enable tile culling
    fillCmd->header.cullTileMask = 0;

    return SDKErr_OK;
}


int gfx_draw_begin_frame_tile3d( float fovy, float aspect, float zNear, float zFar, uint16_t fill, bool clearZBuffer, bool clearAttrBuffer )
{
    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_BeginFrameTile3D* fillCmd = (GPUCMD_BeginFrameTile3D*)gfx_alloc_raw(sizeof(GPUCMD_BeginFrameTile3D));
    if(!fillCmd)
        return SDKErr_Fail;

    GPU_INIT_CMD(fillCmd, EGPUCMD_BeginFrameTile3D);
    fillCmd->fovy = fovy;
    fillCmd->aspect = aspect;
    fillCmd->zNear = zNear;
    fillCmd->zFar = zFar;
    fillCmd->fill = fill;
    fillCmd->clearZBuffer = clearZBuffer;
    fillCmd->clearAttrBuffer = clearAttrBuffer;
    
    // enable tile culling
    fillCmd->header.cullTileMask = 0;

    return SDKErr_OK;
}


int gfx_upload_mesh3d( uint8_t vdpId, uint8_t arenaId, const float* vertices, size_t numVerts, const float* texCoords, size_t numTexCoords, 
    const float* normals, size_t numNormals, const uint16_t* faces, size_t numTris, size_t numfaces, const float* bounds,
    const struct GpuMeshMaterial3D* mat )
{
    // Upload verts
    uint32_t vertsBufferId = gfx_upload_buffer( vdpId, arenaId, (const uint8_t*)vertices, numVerts * sizeof(float) * 3, 0, numVerts, 0 );

    // Upload tex
    uint32_t texCoordsBufferId = gfx_upload_buffer( vdpId, arenaId, (const uint8_t*)texCoords, numTexCoords * sizeof(float) * 2, 0, numTexCoords, 0 );    

    // Upload normals
    uint32_t normalsBufferId = gfx_upload_buffer( vdpId, arenaId, (const uint8_t*)normals, numNormals * sizeof(float) * 3, 0, numNormals, 0 );    

    // Upload faces
    uint32_t facesBufferId = gfx_upload_buffer( vdpId, arenaId, (const uint8_t*)faces, sizeof(uint16_t) * numfaces, 0, numfaces, numTris );    

    
    // Create meshInfo buffer & bind buffers
    GpuMesh3DBufferInfo meshBuffer = {};
    meshBuffer.vertsBufferId = vertsBufferId;
    meshBuffer.texCoordsBufferId = texCoordsBufferId;
    meshBuffer.normalsBufferId = normalsBufferId;
    meshBuffer.facesBufferId = facesBufferId;    
    memcpy(meshBuffer.bounds, bounds, sizeof(meshBuffer.bounds) );
    if( mat )
        meshBuffer.mat = *mat;

    uint32_t meshBufferId = gfx_upload_buffer( vdpId, arenaId, (const uint8_t*)&meshBuffer, sizeof(meshBuffer), 0, 0, 0 );    

    return meshBufferId;
}


// C versions of the TGX c++ math functions
void gfx_matrix_set_identity( struct Matrix4* MIn )
{
    float*M = MIn->M;
    memset(M, 0, 16 * sizeof(float));
    M[0] = M[5] = M[10] = M[15] = ((float)1);
}


struct Matrix4 gfx_matrix_mult( Matrix4* A, Matrix4* B )
{
    struct Matrix4 R; 
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            R.M[i + j*4] = (float)0;
            for (int k = 0; k < 4; k++) { R.M[i + j * 4] += A->M[i + k * 4] * B->M[k + j * 4]; }
        }
    }
    return R;
}


void gfx_matrix_set_translate( struct Matrix4* MIn, float x, float y, float z )
{
    float*M = MIn->M;
    memset(M, 0, 16 * sizeof(float));
    M[0] = (float)1;
    M[5] = (float)1;
    M[10] = (float)1;
    M[12] = x;
    M[13] = y;
    M[14] = z;
    M[15] = (float)1;    
}


void gfx_matrix_set_scale( struct Matrix4* MIn, float x, float y, float z)
{
    float*M = MIn->M;
    memset(M, 0, 16 * sizeof(float));
    M[0] = x;
    M[5] = y;
    M[10] = z;
    M[15] = (float)1;
}


void gfx_matrix_set_rotate( struct Matrix4* MIn, float angle, float x, float y, float z )
{
    float*M = MIn->M;

    static const float deg2rad = (float)(M_PI / 180);
    const float norm = sqrt(x*x + y*y + z*z);
    if (norm == 0) { 
        gfx_matrix_set_identity( MIn ); 
        return;
    }
    const float nx = x / norm;
    const float ny = y / norm;
    const float nz = z / norm;
    const float c = cos(deg2rad * angle);
    const float oneminusc = ((float)1) - c;
    const float s = sin(deg2rad * angle);

    memset(M, 0, 16 * sizeof(float));
    M[0] = nx * nx * oneminusc + c;
    M[1] = ny * nx * oneminusc + nz * s;
    M[2] = nx * nz * oneminusc - ny * s;
    M[4] = nx * ny * oneminusc - nz * s;
    M[5] = ny * ny * oneminusc + c;
    M[6] = ny * nz * oneminusc + nx * s;
    M[8] = nx * nz * oneminusc + ny * s;
    M[9] = ny * nz * oneminusc - nx * s;
    M[10] = nz * nz * oneminusc + c;
    M[15] = (float)1;    
}


void gfx_matrix_mult_rotate( struct Matrix4* MIn, float angle, float x, float y, float z )
{    
    struct Matrix4 mat;
    gfx_matrix_set_rotate( &mat, angle, x, y, z );
    *MIn = gfx_matrix_mult( &mat, MIn ); // mat * self(MIn)    
}


void gfx_matrix_mult_translate( struct Matrix4* MIn, float x, float y, float z)
{
    struct Matrix4 mat;
    gfx_matrix_set_translate( &mat, x, y, z);
    *MIn = gfx_matrix_mult( &mat, MIn ); // mat * self(MIn)    
}        


int gfx_draw_triangle_tex(     
    float P1x, float P1y, float P1z,
    float P2x, float P2y, float P2z,
    float P3x, float P3y, float P3z,
    float N1x, float N1y, float N1z,
    float N2x, float N2y, float N2z,
    float N3x, float N3y, float N3z,
    float T1x, float T1y,
    float T2x, float T2y,
    float T3x, float T3y,
    uint16_t textureBufferId, 
    uint16_t color, 
    bool culling )
{
    if(!g_Gfx)
        return SDKErr_Fail;

    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_DrawTriTex* fillCmd = (GPUCMD_DrawTriTex*)gfx_alloc_raw(sizeof(GPUCMD_DrawTriTex));
    if(!fillCmd)
    {
        printf("cmd overflow at %d\n", g_Gfx->triCnt);
        return SDKErr_Fail;
    }

    GPU_INIT_CMD(fillCmd, EGPUCMD_DrawTriTex);
    fillCmd->P1x = P1x; fillCmd->P1y = P1y; fillCmd->P1z = P1z;    
    fillCmd->P2x = P2x; fillCmd->P2y = P2y; fillCmd->P2z = P2z;    
    fillCmd->P3x = P3x; fillCmd->P3y = P3y; fillCmd->P3z = P3z;    

    fillCmd->N1x = N1x; fillCmd->N1y = N1y; fillCmd->N1z = N1z;    
    fillCmd->N2x = N2x; fillCmd->N2y = N2y; fillCmd->N2z = N2z;    
    fillCmd->N3x = N3x; fillCmd->N3y = N3y; fillCmd->N3z = N3z;    

    fillCmd->T1x = T1x; fillCmd->T1y = T1y;
    fillCmd->T2x = T2x; fillCmd->T2y = T2y;
    fillCmd->T3x = T3x; fillCmd->T3y = T3y;

    fillCmd->textureBufferId = textureBufferId;
    fillCmd->color = color;
    fillCmd->culling = culling;
    
    // enable tile culling        
    fillCmd->header.cullTileMask = 0;

    g_Gfx->triCnt++;

    return SDKErr_OK;  
}


int gfx_set_lookAt3d( float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ, float upX, float upY, float upZ )
{
    if(!g_Gfx)
        return SDKErr_Fail;

    struct VdpClientImpl_t* client = display_get_impl();
    if(!g_Gfx || !client)
        return SDKErr_Fail;

    struct GPUCMD_LookAt3D* fillCmd = (GPUCMD_LookAt3D*)gfx_alloc_raw(sizeof(GPUCMD_LookAt3D));
    
    if(!fillCmd)
    {
        printf("cmd overflow at %d\n", g_Gfx->triCnt);
        return SDKErr_Fail;
    }

    GPU_INIT_CMD(fillCmd, EGPUCMD_LookAt3D);
    fillCmd->eyeX = eyeX; fillCmd->eyeY = eyeY; fillCmd->eyeZ = eyeZ;    
    fillCmd->centerX = centerX; fillCmd->centerY = centerY; fillCmd->centerZ = centerZ;    
    fillCmd->upX = upX; fillCmd->upY = upY; fillCmd->upZ = upZ;

    return SDKErr_OK; 
}