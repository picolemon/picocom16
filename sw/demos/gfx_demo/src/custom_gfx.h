/** Custom gpu types
*/
#include "gpu/gpu.h"


/** Shader fill */
typedef struct __attribute__((__packed__)) GPUCDM_DrawLine
{    
    GpuCmd_Header header; 
    uint16_t p0X;
    uint16_t p0Y;
    uint16_t p1X;
    uint16_t p1Y;
    col16 col;
} GPUCDM_DrawLine;

typedef struct __attribute__((__packed__)) GPUCDM_DrawTri
{    
    GpuCmd_Header header; 
    uint8_t drawMode;
    //vertex_t p0, p1, p2;   
    col16 col;
} GPUCDM_DrawTri;

/** Rad grad cmd */
typedef struct __attribute__((__packed__)) GPUMD_DrawRadialGrad
{    
    GpuCmd_Header header; 
    col16 fromCol;
    col16 toCol;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;    
    uint16_t pivotX;
    uint16_t pivotY;    
    uint16_t length;    
} GPUMD_DrawRadialGrad;

