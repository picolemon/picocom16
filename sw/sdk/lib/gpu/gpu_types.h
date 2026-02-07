#pragma once

#include "picocom/platform.h"
#include <stdint.h>


// config
#define GPU_CMD_MAX_DATA_SZ 8192        // Max command data buffer size  (GpuCommandList_t alloc size)
#define GPU_MAX_SHADER_CMD_ID 128       // Max dispatch/cmd id of draw command
#define GPU_MAX_BUFFER_ID 256           // Max buffer ids
#define GPU_FLASH_BUFFER_PAGE_SIZE 4096 // Flash buffers are fixed to 4k pages
#define GPU_FLASH_MAX_WRITE 1024        // Max flash writes per boot
#define GPU_TILEMAP_TILE_SZ 16          // Fixed tilemap size
#define GPU_TILE_MAX_FRAMES 2   //8           // Max number of frames in tilemap anims/overlays [change] from 8 -> 2 ram optim
#define GPU_TILE_MAX_LAYERS 8           // Max tilemap layers

// Rendering impl
#define GPU_ATTR_ALPHA_MASK 0b00001111  // Alpha bits (16 shades)
#define GPU_ATTR_PIXELWRITE0_MASK 0b10000000  // Pixel write flag0 ( useful for water blend policy )

// fwd
struct VDP1CMD_DrawCmdData;

// macros
#define CEIL_INT(a, b) (((a) / (b)) + (((a) % (b)) > 0 ? 1 : 0))
#define FLOOR_INT(a, b) (((a) / (b)) )


// Init command header helper
#define GPU_INIT_CMD(gpuCmd, cmdId) \
    memset(gpuCmd, 0, sizeof(*gpuCmd)); \
    (gpuCmd)->header.cmd = cmdId; \
    (gpuCmd)->header.flags = 0; \
    (gpuCmd)->header.sz = sizeof(*gpuCmd); \
    

/** Fixed affine xform */
typedef int32_t affine_transform[6];    


/** Gpu errors */
enum EGpuErrorCode
{
    EGpuErrorCode_None,
    EGpuErrorCode_General,
    EGpuErrorCode_InvalidBufferId,
    EGpuErrorCode_FlashWriteMax,
    EGpuErrorCode_OutOfBounds,
};


/* Command grouping.
*/
enum EGPUBufferArena
{   
    EGPUBufferArena_Ram0        = 0,        // Ram store 0
    EGPUBufferArena_Flash0      = 1,       // Flash store 0 
};


/** Blend mode */
enum EBlendMode 
{
    EBlendMode_None,
    EBlendMode_Add,
    EBlendMode_Alpha,
    EBlendMode_ColorKey,
    EBlendMode_Multiply,
    EBlendMode_FillMasked,
    EBlendMode_ColkeyAlpha,
    EBlendMode_DebugAttrAlpha,
    EBlendMode_DebugAttrPixelWriteMask,
    EBlendMode_ColorKeyTintAdd,
};


/* GPU command types
*/
enum EGPUCmd
{   
    EGPUCMD_NOP = 0,
    EGPUCMD_ResetGpu,
    EGPUCMD_SetDebug,
    EGPUCMD_SetClearCol,
    EGPUCMD_FillRectCol,
    EGPUCMD_CreateBuffer,
    EGPUCMD_WriteBufferData,
    EGPUCMD_BlitRect,
    EGPUCMD_RegisterCmd,    
    EGPUCMD_DrawLine,        
    EGPUCMD_InitRenderer3D,
    EGPUCMD_BeginFrameTile3D,
    EGPUCMD_SetShader3D,
    EGPUCMD_SetMatrix3D,    
    EGPUCMD_DrawTriTex,
    EGPUCMD_DrawMesh3D,
    EGPUCMD_LookAt3D,
    EGPUCMD_DrawTileMap,        
    EGPUCMD_DrawWater,
    EGPUCMD_CreateLinkedTilemapBuffer,
    EGPUCMD_CompositeTile,
    // User cmds
    EGPUCMD_UserCmdBegin = 64,  // Start of user non-std cmds when registering custom gpu cmds
};


/** Texture format */
enum ETextureFormat
{
    ETextureFormat_None = 0,
    ETextureFormat_1BPP,            // 1 bit per pixel
    ETextureFormat_8BPP,            // 8 bits per pixel
    ETextureFormat_RGB16,           // 16BPP
    ETextureFormat_RGBA16,          // 16BPP + 8bit alpha
};


/** Color 16 int type */
typedef uint16_t col16;
#define COL16_MAX_R     31
#define COL16_MAX_G     63
#define COL16_MAX_B     31


/** 16bit 565 RGB Color
*/
typedef struct __attribute__((__packed__)) Col16_t
{    
    union {
        struct {
            uint16_t b : 5;
            uint16_t g : 6;
            uint16_t r : 5;
        };
        col16 value;
    };
} Col16_t;



/** 32bit RGB Color
*/
typedef struct __attribute__((__packed__)) ColRGBF_t
{    
    float r, g, b;
} ColRGBF_t;


/** Gpu cmd flags
 */
enum EGpuCmd_Header_Flags
{
    EGpuCmd_Header_Flags_None,
    EGpuCmd_Header_Flags_TileCullMask = 1 << 0,
};


/** Render background col when rendering */
typedef struct __attribute__((__packed__)) GpuCmd_Header
{    
    uint8_t cmd;            // command type (EGPUCmd)    
    uint16_t sz;            // size of command    
    uint8_t flags;          // command flags
    uint16_t cullTileMask;  // Per cmd tile cull
} GpuCmd_Header;


/** Reset gpu */
typedef struct __attribute__((__packed__)) GPUCMD_ResetGpu
{    
    GpuCmd_Header header; 
    uint8_t cmds;
    uint8_t buffers;
    uint8_t stats;
} GPUCMD_ResetGpu;


/** Set gpu demo mode */
typedef struct __attribute__((__packed__)) GPUCMD_SetDebug
{    
    GpuCmd_Header header; 
    uint8_t dumpBufferUploads;
} GPUCMD_SetDebug;


/** Debug dump cmd */
typedef struct __attribute__((__packed__)) GPUCMD_DebugDump
{    
    GpuCmd_Header header;     
    uint16_t dumpBufferId;  // valid < 0xffff 
    uint8_t logConsole; // dump to console
} GPUCMD_DebugDump;


/** Render background col when rendering */
typedef struct __attribute__((__packed__)) GPUCMD_SetClearCol
{    
    GpuCmd_Header header; 
    uint16_t col;          
} GPUCMD_SetClearCol;


/** Fill solid rect */
typedef struct __attribute__((__packed__)) GPUCMD_FillRectCol
{    
    GpuCmd_Header header; 
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;      
    uint16_t col;    
    uint8_t a;   
    uint8_t blendMode;
} GPUCMD_FillRectCol;


/** Create/setup buffer */
typedef struct __attribute__((__packed__)) GPUCMD_CreateBuffer
{    
    GpuCmd_Header header; 
    uint8_t arena;                  // buffer type EGPUBufferArena   
    uint16_t bufferId;              // buffer index
    uint32_t memOffset;             // offset in mem (ram or rom)
    uint32_t memSize;               // size in memory
    uint8_t textureFormat;          // Texture format ETextureFormat
    uint16_t w, h;                  // Texture size    
    uint16_t parentBufferId;        // Buffer id of parent eg. data pack     
} GPUCMD_CreateBuffer;


/** Link tilemap edges for fast lookup */
typedef struct __attribute__((__packed__)) GPUCMD_CreateLinkedTilemapBuffer
{
    GpuCmd_Header header; 
    uint16_t tilemapDataBufferId;                      // main buffer index, middle index or index[4] of edge map.
    uint16_t tilemapStateBufferId;                      // main buffer index, middle index or index[4] of edge map.
    uint16_t edgeTilemapDataBufferIds[9];   // Tile edge lookup for tilemap auto tile / fluid data samplers, efficient buffer access accros tile boundries
    uint16_t edgeTilemapStateBufferIds[9];        
} GPUCMD_CreateLinkedTilemapBuffer;


/** Set render state blend mode */
typedef struct __attribute__((__packed__)) GPUCMD_SetBlendMode
{    
    GpuCmd_Header header; 
    uint8_t blendMode;
} GPUCMD_SetBlendMode;



/** Composite tile into frame buffer */
typedef struct __attribute__((__packed__)) GPUCMD_CompositeTile
{    
    GpuCmd_Header header; 
    uint8_t blendMode;
    uint16_t palBufferId;
} GPUCMD_CompositeTile;


/** Tile render type */
enum EGPUAttr_ETileRenderType
{
    EGPUAttr_ETileRenderType_None,
    EGPUAttr_ETileRenderType_AutoTile,
    EGPUAttr_ETileRenderType_AutoTileFluid,
    EGPUAttr_ETileRenderType_Decals,
    EGPUAttr_ETileRenderType_DecalsFromMass,
};


/** Tilemap frame data */
typedef struct __attribute__((__packed__)) GPUAttr_TileFrame
{
    uint16_t x, y;       // offset in packed tilemap texture 
} GPUAttr_TileFrame;


/** Tilemap data */
typedef struct __attribute__((__packed__)) GPUAttr_TileDataLayer
{
    uint8_t type; // [EGPUAttr_ETileRenderType] base tile render type
    uint32_t tileGroup;     // tile auto-tile grouping    
    uint16_t textureBufferId;
    uint16_t palBufferId;
    uint8_t tileFrameCnt;
    struct GPUAttr_TileFrame tileFrames[GPU_TILE_MAX_FRAMES];
    float animFrameTime;
    uint8_t frameSeed;
    uint8_t maskPrevLayer;
    uint8_t numHTiles;      // decal state variants
    uint8_t stateDiv;
    uint8_t minState;
} GPUAttr_TileDataLayer;


/** Data for GPUCMD_DrawTileMap command */
typedef struct __attribute__((__packed__)) GPUAttr_DrawTileMapData
{        
    uint32_t debugId;
    uint8_t isValid;
    uint8_t isFluid;
    uint8_t tileGroup;    
    //uint16_t debugCol;
    uint8_t tileId;
    uint8_t layerCnt;
    struct GPUAttr_TileDataLayer layers[GPU_TILE_MAX_LAYERS];
    uint32_t overlayTileId; // special case for bubbles overlay
    uint8_t writeAlpha; 
    bool blendAdditive;
} GPUAttr_DrawTileMapData;


/** Draw tile map */
typedef struct __attribute__((__packed__)) GPUCMD_DrawTileMap
{    
    GpuCmd_Header header;     
    uint16_t tilemapDataBufferId;
    uint16_t tilemapStateBufferId;
    uint16_t tilemapAttribBufferId;    
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;    
    uint8_t tileIdMask;
    uint8_t tileGroupMask;
    float time;     // time for frame timers    
    uint32_t seed;
    uint8_t blendMode;
} GPUCMD_DrawTileMap;


/** GPUCMD_WriteBufferData flags */
enum EGPUCMD_WriteBufferDataFlags {
    EGPUCMD_WriteBufferDataFlags_commitPage = 1 << 0,
    EGPUCMD_WriteBufferDataFlags_finalPage = 1 << 1,
    EGPUCMD_WriteBufferDataFlags_lockWrites = 1 << 2,
    EGPUCMD_WriteBufferDataFlags_firstPage = 1 << 3,    // Prepare flash writer
};


/** Write buffer data */
typedef struct __attribute__((__packed__)) GPUCMD_WriteBufferData
{    
    GpuCmd_Header header; 
    uint8_t bufferId;           // buffer index
    uint8_t flags;              // buffer flags
    uint32_t offset;            // write offset
    uint16_t dataSize;          // data size
    bool allowNonTileZero;
    // [dyn]   
    uint8_t data[32];           // [must be last entry] Command data (any size)  
    // [dont add members > cmdData]    
} GPUCMD_WriteBufferData;


/** Register GPU cmd */
typedef struct __attribute__((__packed__)) GPUCMD_RegisterCmd
{    
    GpuCmd_Header header; 
    uint8_t vdpId;
    uint8_t cmdId;              // cmd id
    uint8_t bufferId;           // buffer of executable data    
} GPUCMD_RegisterCmd;


/** Simple line draw */
typedef struct __attribute__((__packed__)) GPUCMD_DrawLine
{    
    GpuCmd_Header header; 
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;    
    uint16_t col;
} GPUCMD_DrawLine;


/** Draw tex tri */
typedef struct __attribute__((__packed__)) GPUCMD_DrawTriTex
{    
    GpuCmd_Header header; 
    float P1x, P1y, P1z;
    float P2x, P2y, P2z;
    float P3x, P3y, P3z;

    float N1x, N1y, N1z;
    float N2x, N2y, N2z;
    float N3x, N3y, N3z;

    float T1x, T1y;
    float T2x, T2y;
    float T3x, T3y;

    uint16_t textureBufferId;

    uint16_t color;

    bool culling;

} GPUCMD_DrawTriTex;


/** Set camera lookat */
typedef struct __attribute__((__packed__)) GPUCMD_LookAt3D
{    
    GpuCmd_Header header; 
    float eyeX, eyeY, eyeZ;
    float centerX, centerY, centerZ;
    float upX, upY, upZ;

} GPUCMD_LookAt3D;



/** Simple blitter */
typedef struct __attribute__((__packed__)) GPUCMD_BlitRect
{    
    GpuCmd_Header header; 
    uint8_t bufferId;       // Texture buffer
    uint16_t srcX;
    uint16_t srcY;    
    int16_t dstX;
    int16_t dstY;
    uint16_t w;
    uint16_t h;      
    uint16_t colKey;        // Colkey for 16BPP modes, transparent index for palette modes
    uint16_t palBufferId;   // Palette buffer id
    uint8_t flags;
    uint8_t blendMode;
    uint8_t a;              // Alpha blend mode
    uint8_t writeAlpha;     // Write alpha
} GPUCMD_BlitRect;


/** Init renderer 3D */
typedef struct __attribute__((__packed__)) GPUCMD_InitRenderer3D
{    
    GpuCmd_Header header; 
} GPUCMD_InitRenderer3D;


/** Init renderer 3D */
typedef struct __attribute__((__packed__)) GPUCMD_BeginFrameTile3D
{    
    GpuCmd_Header header; 
    float fovy;
    float aspect;
    float zNear;
    float zFar;
    uint16_t fill;
    bool clearZBuffer;
    bool clearAttrBuffer;
} GPUCMD_BeginFrameTile3D;


/** Set shader 3D */
typedef struct __attribute__((__packed__)) GPUCMD_SetShader3D
{    
    GpuCmd_Header header; 
    uint32_t shaderId;
} GPUCMD_SetShader3D;


/** Set Matrix 3D */
typedef struct __attribute__((__packed__)) GPUCMD_SetMatrix3D
{    
    GpuCmd_Header header; 
    float M[16];
} GPUCMD_SetMatrix3D;


/** Mesh material */
typedef struct GpuMeshMaterial3D
{
    float r, g, b;
    float ambientStrength;
    float diffuseStrength;
    float specularStrength;
    int specularExponent;
} GpuMeshMaterial3D;


/** Mesh buffer for mesh drawing*/
typedef struct __attribute__((__packed__)) GpuMesh3DBufferInfo
{    
    GpuCmd_Header header; 
    uint32_t vertsBufferId;
    uint32_t texCoordsBufferId;
    uint32_t normalsBufferId;
    uint32_t facesBufferId;    
    float bounds[6];
    GpuMeshMaterial3D mat;
} GpuMesh3DBufferInfo;


/** Draw mesh 3D */
typedef struct __attribute__((__packed__)) GPUCMD_DrawMesh3D
{    
    GpuCmd_Header header; 
    uint32_t meshBufferId;
    uint32_t textureBufferId;
    bool culling;
} GPUCMD_DrawMesh3D;


/** Tilemap data for GPUCMD_BlitTile*/
typedef struct __attribute__((__packed__)) BlitTile
{
    GPUCMD_BlitRect blit; // tile blit info
} BlitTile;


/** Tilemap data for GPUCMD_BlitTile, holds array of BlitTile to render to screen*/
typedef struct __attribute__((__packed__)) BlitTilemapData
{
    int16_t tileW;  // Tile size
    int16_t tileH;
    uint16_t tilesCnt;  // tiles count (dynamic cmd)
    BlitTile tiles[0]; // tiles
} BlitTilemapData;


/** Blit tile */
typedef struct __attribute__((__packed__)) GPUCMD_BlitTile
{    
    GpuCmd_Header header; 
    uint16_t textureBufferId;           // Tile texture buffer
    uint16_t tileDataBufferId;          // Tile data buffer    
    int16_t dstX;
    int16_t dstY;
    uint16_t w;
    uint16_t h;      
} GPUCMD_BlitTile;


/** Affine blitter */
typedef struct __attribute__((__packed__)) GPUCMD_BlitRectAffine
{    
    GpuCmd_Header header; 
    uint16_t bufferId;          // Texture buffer id       
    int16_t dstX;
    int16_t dstY;
	uint8_t log_size; // always square
	bool has_opacity_metadata;
	bool hflip;
	bool vflip;
    affine_transform xform;
} GPUCMD_BlitRectAffine;


/** Command data per bin
*/
typedef struct GpuCommandList_t
{   
    uint32_t headerSz;          // Pre-allocated header for starting offset ( allow prepending of bus command for DMA transfers )
    uint32_t allocSz;           // Alloc size of buffer (cmdData size)
    uint32_t offset;            // Current append position (command data size)
    uint16_t cmdCount;          // Command cnt    
    uint8_t* cmdData;
} GpuCommandList_t;


/** Frame buffer slice, renderer work unit.
*/
typedef struct TileFrameBuffer_t
{
    uint8_t colorDepth;
    uint16_t tileId;
    uint8_t* pixelsData;
    uint8_t* attr;      // alpha/attr buffer 
    uint32_t y; // start row
    uint32_t w;
    uint32_t h;
} TileFrameBuffer_t;


/** Gpu command access type */
enum EGpuCmdListAccessType
{
    EGpuCmdListAccessType_None = 0,
    EGpuCmdListAccessType_Write,
    EGpuCmdListAccessType_ReadShared,
};


/** List of command lists */
typedef struct GpuCommandListList_t
{
    GpuCommandList_t** lists;
    uint32_t listAllocCnt;  // Total list cnt
    uint32_t offset;        // Append offset
    uint32_t submitId;      // Unique frame/submit id
    uint8_t completeFlags;  // Source complete flags
    uint32_t accessType;        // Lock info [EGpuCmdListAccessType]
} GpuCommandListList_t;


/** Command lists double buffer */
typedef struct GpuCommandListListDoubleBuffer
{
    GpuCommandListList_t* buffers[2];     // Front & back double buffers, one is reading and the other is writing based on currentBufferId % 1 etc.
    uint32_t currentBufferId;           // Active read/write buffer
} GpuCommandListListDoubleBuffer;


/** Gpu status
*/
typedef struct __attribute__((__packed__)) GpuFrameStats_t
{    
    bool isValid;           // Stats valid
    uint32_t cmdSeqNum;			
    uint32_t frameTime;     // Total frame render time
    uint32_t tileCnt;       // Num tiles rendered
    uint32_t tileMaxTime;   // Max exec time of tile        
    uint32_t cmdErrors;     // Gpu error counts    
} GpuFrameStats_t;


/** Detailed frame profile data */
typedef struct __attribute__((__packed__)) GpuFrameProfile
{
    bool isValid;                   // Stats valid
    uint32_t cmdSeqNum;			
    //uint32_t cmdExecCounts[GPU_MAX_SHADER_CMD_ID];   // Per command exec count    
    //uint32_t cmdMaxTime[GPU_MAX_SHADER_CMD_ID];      // Max exec per cmd
    uint32_t frameCullCounts;                         // Total frame culled cmds    
} GpuFrameProfile;


/** Buffer info */
typedef struct GpuBufferInfo
{
    bool isValid;           // Buffer created
    uint8_t* basePtr;       // mem address
    uint32_t arenaId;       // alloc arena id ( ram, flash etc.  )
    uint32_t arenaOffset;        // alloc offset in arena 
    uint32_t size;          // size of buffer
    uint8_t textureFormat;  // Texture format ETextureFormat
    uint16_t w, h;          // Texture size
    struct GpuBufferInfo* edgeData[9]; // Connected tile edge for north, east, south etc arranged as 3x3 row first matrix
    uint32_t writeCnt;      // Writes to buffer
    uint32_t finalWriteCnt; // Writes to buffer
    bool locked;            // Lock for writing until buffer re-creatoin
} GpuBufferInfo;


/** Gpu exported methods */
enum EGpuJitMethod {
    EGpuJitMethod_sqrtf
};


/** Draw texture flags */
enum EDrawTextureFlags
{
    EDrawTextureFlags_FlippedX = 1 << 0,
};

// Exported Gpu methods
typedef float (*SqrtCallback)(float);