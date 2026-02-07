/*
Graphics library
*/
#pragma once
#include "picocom/platform.h"
#include "gpu/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif


// Config
#define MAX_ASSET_PACKID    32
#define GFX_TEMP_BUFFER_ID  0       // Temp buffer id for storing temp state between cmds
#define GFX_TEMP_BUFFER_SZ  128    // Temp buffer reserved
#define GFX_TEMP_BUFFER_BASE 16       // Reserved buffer ids
#define GFX_TEMP_SCANLINE_PAL_ID 1      // Scanline pal id
#define GFX_TEMP_SCANLINE_BUFFER_ID 2   // Scanline buffer id


/** Target VDP id */
enum EGfxTargetVDP
{
    EGfxTargetVDP1,
    EGfxTargetVDP2,
};


/* Packed asset resource info */
typedef struct __attribute__((__packed__)) GfxAssetPack
{
    uint32_t packId;        // Unique pack id    
    uint32_t size;          // Size of asset data
    uint8_t* basePtr;       // Embeded asset ptr in flash (or ram), Should contain GfxResourceInfo[] offset by its record size.    
    uint8_t* compressedBasePtr; // Embeded asset ptr in flash (or ram), Should contain raw compressed blocks, encoded asset should have an offset in this region 
    uint32_t compressedSize;    // Sizeof of compressedBasePtr
    uint32_t uncompressedSize;  // Compressed data inflated size ( size of expanded pack data in ram )
    uint16_t assetCount;    // Num assets (assetOffsets count)
    uint32_t* assetOffsets; // Resource offset map
    bool compressed;        // Pack compressed in 4k blocks
    bool isInPlace;         // Used by IMPL_EMBED_FAST_BUILD for already inplace flash assets on hardware
} GfxAssetPack;


/** Uploaded pack info to vdp
 */
typedef struct GfxAssetPackUpload
{
    const GfxAssetPack* pack;   // Source pack
    uint32_t vdpId;             // VDP uploaded to
    uint32_t baseBufferId;      // Parent buffer for assets ( creating buffers can ref parent with offset )
    uint32_t baseAddress;       // Flash/ram base address 
    uint8_t arenaId;
} GfxAssetPackUpload;


/** Asset types (graphics & audio) */
enum EGfxAssetType {
    EGfxAssetType_None,
    EGfxAssetType_FontInfo,
    EGfxAssetType_Texture,
    EGfxAssetType_Mesh,
    EGfxAssetType_Spline,
    EGfxAssetType_AnimInfo,
    EGfxAssetType_PCM,
    EGfxAssetType_Ogg,
    // custom types
    EGfxAssetType_Custom = 128
};


/* Packed resource info */
typedef struct __attribute__((__packed__)) GfxResourceInfo
{
    uint8_t type;       // Asset type
    uint32_t size;      // Resource record size ( max 64k )
    uint8_t flags;      // Resource flags
} GfxResourceInfo;


/* Font glyph info */
typedef struct __attribute__((__packed__)) GfxTextureInfo
{        
    GfxResourceInfo header;      
    uint8_t format;     // Texture format
    uint16_t w, h;      // Texture size
    uint32_t palAssetId;    // Palette asset id for indexed 
    uint32_t dataSize;
    uint32_t dataOffset;
    uint8_t data[0];    // Texture data
} GfxTextureInfo;  


/* Font glyph info */
typedef struct __attribute__((__packed__)) GfxGlyphInfo
{
    uint16_t c;                 // Char codepoint    
    int16_t x, y;   
    int8_t w, h;                // Pos and size
    uint8_t advance;            // character advance
    int8_t offsetX, offsetY;    // char offset
} GfxGlyphInfo;  


/* Font info */
typedef struct __attribute__((__packed__)) GfxFontInfo
{
    GfxResourceInfo header;
    uint32_t fontTextureAssetId;    // Font texture asset id
    uint8_t spaceWidth;             // Font space width
    uint8_t lineHeight;             // Line height
    uint16_t glyphCnt;              // glyphs[] count    
    GfxGlyphInfo glyphs[0];
} GfxFontInfo;


/* Anim frame info */
typedef struct __attribute__((__packed__)) GfxFrameInfo
{    
    uint16_t x, y, w, h;            // frame rect
    uint16_t pivotX, pivotY;
} GfxFrameInfo;  


/* Anim info */
typedef struct __attribute__((__packed__)) GfxAnimInfo
{
    GfxResourceInfo header;
    uint32_t assetId;               // Anim texture asset id
    uint16_t frameCnt;              // glyphs[] count    
    GfxFrameInfo frames[0];
} GfxAnimInfo;


/* Mesh vec */
typedef struct __attribute__((__packed__)) Vec3dInfo
{    
    float x,y,z,w;
} VertInfo;


/* Mesh info */
typedef struct __attribute__((__packed__)) MeshInfo
{
    GfxResourceInfo header;
    uint32_t verticesOffset;        // Offset in data ( float*3 )
    uint32_t verticesCount;         // Num verts 
    uint32_t texCoordOffset;        // Offset in data ( float*2 )
    uint32_t texCoordCount;         // Num tex coords
    uint32_t normalsOffset;         // Offset in data ( float*3 )
    uint32_t normalsCount;          // Num normals
    uint32_t facesOffset;
    uint32_t nb_faces;
    uint32_t len_face;
    float defaultColor[3];
    float lighting[4];
    float bounds[6];

    uint8_t data[0]; // Packed mesh data 
} MeshInfo;


/* Spline info */
typedef struct __attribute__((__packed__)) SplineInfo
{
    GfxResourceInfo header;
    uint32_t pCnt;          // Num verts
    struct Vec3dInfo p[0]; // Verts (V3F format)
} SplineInfo;


/* Audio info */
typedef struct __attribute__((__packed__)) AudioInfoAsset
{
    GfxResourceInfo header;    
    uint8_t format;         // PCM=0, Ogg=1
    uint8_t channels;       // Audio channels
    uint8_t isCompressed;   // PCM data zlib compressed
    uint32_t uncompressedSize;
    uint32_t dataSize;    
    uint8_t data[0];        // PCM data
} AudioInfoAsset;  


/** Color defs */
enum EColor16BPP
{
    EColor16BPP_Black = 0,
    EColor16BPP_White = 0b1111111111111111,
    EColor16BPP_Red =   0b1111100000000000,
    EColor16BPP_Green =   0b0000011111100000,
    EColor16BPP_Blue =   0b0000000000011111,
    EColor16BPP_Magenta =   0b1111100000011111,
    EColor16BPP_Yellow =   0b1111111111100000,
    EColor16BPP_Gray =    0b0111101111101111
};


/** Shader draw type ( TGX )
 */
enum GfxShaderId
{    
    GFX_SHADER_PERSPECTIVE = (1 << 0),  
    GFX_SHADER_ORTHO = (1 << 1),        

    GFX_SHADER_NOZBUFFER = (1 << 2),
    GFX_SHADER_ZBUFFER = (1 << 3),
    
    GFX_SHADER_FLAT = (1 << 4),  
    GFX_SHADER_GOURAUD = (1 << 5),

    GFX_SHADER_NOTEXTURE = (1 << 7), 
    GFX_SHADER_TEXTURE = (1 << 8),

    GFX_SHADER_TEXTURE_NEAREST = (1 << 11),
    GFX_SHADER_TEXTURE_BILINEAR = (1 << 12), 

    GFX_SHADER_TEXTURE_WRAP_POW2 = (1 << 13), 
    GFX_SHADER_TEXTURE_CLAMP = (1 << 14) 
};


/** Matrix 4x4 */
typedef struct Matrix4 {
    float M[16];
} Matrix4;


/** Gfx alloc slot */
typedef struct GfxBufferAlloc {
    uint32_t offset;
    uint32_t size;
} BufferAlloc;


/** Bump allocator */
typedef struct GfxAllocPool {    
    uint32_t offset;    // Simple bump offset, static one time 
} GfxAllocPool;


/** Gpu alloc pools */
typedef struct GfxVDPAllocPool {
    struct GfxAllocPool arenas[2];    
    uint32_t bufferId;  // last buffer id alloc
} GfxVDPAllocPool;


/** Gfx impl detail */
typedef struct Gfx_impl
{
    const GfxAssetPack* assetPacks[MAX_ASSET_PACKID];
    GfxVDPAllocPool allocPools[2];
    // state ( reset on begin )
    uint8_t currentVdpId;           // Target vdp    
    uint8_t inFrame;    
    uint32_t triCnt;
} Gfx_impl;


// resource loading of packed resource data
int gfx_init();                                                     // Init gfx lib
void gfx_deinit();                                                  // Cleanup gfx
Gfx_impl* gfx_get_impl();                                           // Get gfx state
uint32_t gfx_upload_buffer( uint8_t vdpId, uint8_t arenaId, const uint8_t* data, uint32_t dataSize, uint8_t format, uint16_t w, uint16_t h ); // Upload raw buffer to vdp
int gfx_mount_asset_pack(const struct GfxAssetPack* assetPack);                                 // Mount asset pack and link resource index 
int gfx_upload_asset_pack(const struct GfxAssetPack* assetPack, struct GfxAssetPackUpload* upload, uint8_t arenaId);  // Upload pack to vdp target
int gfx_upload_buffer_pack( struct GfxAssetPackUpload* upload, uint32_t resourceId );           // Upload buffer to vdp from pack
uint32_t gfx_upload_resource_flash(uint32_t resourceId);                                        // Load resource into vdp1 flash
uint32_t gfx_upload_resource_ram(uint32_t resourceId);                                          // Load resource into vdp1 ram
uint32_t gfx_upload_resource(uint32_t resourceId, uint8_t vdpId, uint8_t arenaId);              // Load resource into vdp
uint32_t gfx_upload_resource_impl(uint32_t resourceId, uint8_t vdpId, uint8_t arenaId, uint32_t* palBufferIdOut);   // Load resource into vdp
const struct GfxResourceInfo* gfx_get_resource_info(uint32_t resId);                            // Get loaded resource info
const struct GfxResourceInfo* gfx_get_resource_info_of_type(uint32_t resId, uint8_t type);      // Get loaded resource info if type
uint32_t gfx_get_resource_offset(uint32_t resId);                                               // Get offset of asset in data pack data stream
const uint8_t* gfx_get_raw_resources(uint32_t resId);                                           // Get raw resource data ptr
uint32_t gfx_get_raw_resource_size(uint32_t resId);                                             // Size of raw resource buffer
bool gfx_alloc_block(uint32_t sz, uint8_t vdpId, uint8_t arenaId, uint32_t* bufferIdOut, uint32_t* memBaseOffsetOut);   // allocate gpu data, buffer id and its allocated mem base offset
bool gfx_alloc_buffer_id(uint8_t vdpId, uint8_t arenaId, uint32_t* bufferIdOut);   // allocate gpu data buffer id only

// asset packs
uint16_t gfx_asset_pack_id(uint32_t resId);
uint16_t gfx_asset_res_id(uint32_t resId);

// buffers
int gfx_internal_temp_buffer(uint8_t groupId, const uint8_t* buffer, uint32_t sz, uint8_t textureFormat, uint16_t w, uint16_t h, uint16_t cullTileMask); // Upload temp buffer
int gfx_internal_get_temp_buffer_id();
int gfx_internal_get_temp_buffer_size();
int gfx_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t col);                // Draw simple line
int gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t col, uint8_t a);       // Fill solid rectangle
int gfx_fill_rect_blended(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t col, uint8_t a, uint8_t blendMode); // Fill solid rectangle

// anim helpers
int gfx_get_anim_offset();

// drawing
int gfx_begin_frame();
int gfx_end_frame();
int gfx_set_vdp(enum EGfxTargetVDP vdpId);              // Set current vdp target
int gfx_set_palette(uint16_t* cols, uint16_t cnt);      // Set current palette for index color blits
int gfx_draw_text(uint32_t fontResourceId, uint32_t fontTextureBufferId, int16_t x, int16_t y, const char* text, uint16_t col); // Draw text to string
int gfx_draw_text_ex(uint32_t fontResourceId, uint32_t fontTextureBufferId, int16_t x, int16_t y, const char* text, uint16_t colKey, uint8_t flags); // Draw text to string
int gfx_get_text_size(uint32_t fontResourceId, uint32_t fontTextureBufferId, const char* text, uint16_t* wOut, uint16_t* hOut);     // Get text size
int gfx_draw_texture(uint32_t texturebufferId, int16_t x, int16_t y, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH, uint16_t colKeyOrPal, uint8_t flags); // Draw texture to screen
int gfx_draw_texture_blendmode(uint32_t texturebufferId, int16_t x, int16_t y, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH, uint16_t colKeyOrPal, uint8_t flags, uint8_t blendMode); // Draw texture to screen
int gfx_draw_texture_blendmode_ex(uint32_t texturebufferId, int16_t x, int16_t y, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH, uint16_t colKeyOrPal, uint8_t flags, uint8_t blendMode, uint16_t palBufferId);
int gfx_draw_tilemap(uint32_t tilemapDataBufferId, uint32_t tilemapStateBufferId, uint32_t tilemapAttribBufferId, int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t tileIdMask, uint8_t tileGroupMask, float time, uint32_t seed); // Draw tilemap, uses attribute map to draw tiles from tilemap data ( index of attributes )
int gfx_draw_water(uint32_t tilemapDataBufferId, uint32_t tilemapStateBufferId, uint32_t tilemapAttribBufferId, int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t tileIdMask, uint8_t tileGroupMask, float time, uint32_t seed); // Draw tilemap, uses attribute map to draw tiles from tilemap data ( index of attributes )
int gfx_link_tilemap(uint16_t tilemapDataBufferId, uint16_t tilemapStateBufferId, uint16_t tilemapDataBufferIds[9], uint16_t tilemapStateBufferIds[9]); // Link tilemap edges
int gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t col); // Draw rect outline
int gfx_debug_set_gpu_debug(uint8_t dumpBufferUploads); // Enable gpu debug
struct GpuCmd_Header* gfx_alloc_raw(uint32_t sz);       // alloc raw gpu cmd

// vdp2 composite
int gfx_composite(uint8_t blendMode);                   // composite vdp1 tile 

// 3d api
int gfx_init_renderer3d();                              // alloc 3d renderer on gpu
int gfx_upload_mesh3d( uint8_t vdpId, uint8_t arenaId, const float* vertices, size_t numVerts, const float* texCoords, size_t numTexCoords, const float* normals, size_t numNormals, 
    const uint16_t* faces, size_t numTris, size_t lenFaces, const float* bounds, const struct GpuMeshMaterial3D* mat );
int gfx_draw_begin_frame_tile3d( float fovy, float aspect, float zNear, float zFar, uint16_t fill, bool clearZBuffer, bool clearAttrBuffer ); // setup render tile before drawing meshes
int gfx_set_shader3d( uint32_t shaderId );              // Set current shader
int gfx_set_model_matrix3d( float* M );                 // Set model matrix
int gfx_draw_mesh3d( uint32_t meshBufferId, uint32_t textureBufferId, bool culling ); // Draw mesh
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
    bool culling );
int gfx_set_lookAt3d( float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ, float upX, float upY, float upZ );

// 3d math
void gfx_matrix_set_identity( struct Matrix4* MIn );
struct Matrix4 gfx_matrix_mult( Matrix4* A, Matrix4* B );
void gfx_matrix_set_translate( struct Matrix4* M, float x, float y, float z );
void gfx_matrix_set_scale( struct Matrix4* M, float x, float y, float z);
void gfx_matrix_set_rotate( struct Matrix4* M, float angle, float x, float y, float z );
void gfx_matrix_mult_rotate( struct Matrix4* M, float angle, float x, float y, float z );
void gfx_matrix_mult_translate( struct Matrix4* M, float x, float y, float z);

#ifdef __cplusplus
}
#endif