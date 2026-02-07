#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include "picocom/devkit.h"
#include "gpu_types.h"
#include "gpu.h"
#include "thirdparty/crc16/crc.h"
#include "command_list.h"
#include "picocom/utils/random.h"
#include "platform/pico/vdp2/hw_vdp2_types.h"
#include "picocom/utils/profiler.h"


//
// c++ externs
void gpu_cmd_impl_InitRenderer3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
void gpu_cmd_impl_BeginFrameTile3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
void gpu_cmd_impl_SetShader3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
void gpu_cmd_impl_SetMatrix3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
void gpu_cmd_impl_DrawMesh3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
void gpu_cmd_impl_DrawTriSolid_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
void gpu_cmd_impl_DrawTriTex_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
void gpu_cmd_impl_LookAt3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);


//
//
void gpu_cmd_impl_InitRenderer3D(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    gpu_cmd_impl_InitRenderer3D_impl( gpu, job, header, fb );
}


bool validate_gpu_cmd_impl_InitRenderer3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_InitRenderer3D));                
    return true;
}


bool toString_gpu_cmd_impl_InitRenderer3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_InitRenderer3D* cmd = (GPUCMD_InitRenderer3D*)header;
    sprintf(buff, " " );
    return true;
}


//
//
void gpu_cmd_impl_BeginFrameTile3D(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    gpu_cmd_impl_BeginFrameTile3D_impl( gpu, job, header, fb );
}


bool validate_gpu_cmd_impl_BeginFrameTile3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_BeginFrameTile3D));                
    return true;
}


bool toString_gpu_cmd_impl_BeginFrameTile3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_BeginFrameTile3D* cmd = (GPUCMD_BeginFrameTile3D*)header;
    sprintf(buff, " " );
    return true;
}


//
//
void gpu_cmd_impl_SetShader3D(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    gpu_cmd_impl_SetShader3D_impl( gpu, job, header, fb );
}


bool validate_gpu_cmd_impl_SetShader3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_SetShader3D));                
    return true;
}


bool toString_gpu_cmd_impl_SetShader3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_SetShader3D* cmd = (GPUCMD_SetShader3D*)header;
    sprintf(buff, "shaderId: %x", cmd->shaderId );
    return true;
}


//
//
void gpu_cmd_impl_SetMatrix3D(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    gpu_cmd_impl_SetMatrix3D_impl( gpu, job, header, fb );
}


bool validate_gpu_cmd_impl_SetMatrix3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_SetMatrix3D));                
    return true;
}


bool toString_gpu_cmd_impl_SetMatrix3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_SetMatrix3D* cmd = (GPUCMD_SetMatrix3D*)header;
    sprintf(buff, "M[%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f]", cmd->M[0], cmd->M[1], cmd->M[2], cmd->M[3], cmd->M[4], cmd->M[5], cmd->M[6], cmd->M[7], cmd->M[8], 
        cmd->M[9], cmd->M[10], cmd->M[11], cmd->M[12], cmd->M[13], cmd->M[14], cmd->M[15]);
    return true;
}



//
//
void gpu_cmd_impl_DrawMesh3D(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    gpu_cmd_impl_DrawMesh3D_impl( gpu, job, header, fb );
}


bool validate_gpu_cmd_impl_DrawMesh3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_DrawMesh3D));                
    return true;
}


bool toString_gpu_cmd_impl_DrawMesh3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_DrawMesh3D* cmd = (GPUCMD_DrawMesh3D*)header;
    sprintf(buff, "meshBufferId: %x", cmd->meshBufferId );
    return true;
}


//
//
void gpu_cmd_impl_DrawTriTex(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    gpu_cmd_impl_DrawTriTex_impl( gpu, job, header, fb );
}


bool validate_gpu_cmd_impl_DrawTriTex(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_DrawTriTex));                
    return true;
}


bool toString_gpu_cmd_impl_DrawTriTex(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_DrawTriTex* cmd = (GPUCMD_DrawTriTex*)header;
    sprintf(buff, " "  );
    return true;
}


//
//
void gpu_cmd_impl_LookAt3D(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    gpu_cmd_impl_LookAt3D_impl( gpu, job, header, fb );
}


bool validate_gpu_cmd_impl_LookAt3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, struct GpuValidationOutput_t* info)
{    
    GPU_VALIDATE_ASSERT(header->sz == sizeof(GPUCMD_LookAt3D));                
    return true;
}


bool toString_gpu_cmd_impl_LookAt3D(const struct GpuState_t* gpu, struct GpuCmd_Header* header, char* buff, uint32_t buffSz)
{
    struct GPUCMD_LookAt3D* cmd = (GPUCMD_LookAt3D*)header;
    sprintf(buff, " "  );
    return true;
}



void gpu_init_commands_3d(GpuState_t* state)
{
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="EGPUCMD_InitRenderer3D", .id=EGPUCMD_InitRenderer3D, .cmd=gpu_cmd_impl_InitRenderer3D, .validator=validate_gpu_cmd_impl_InitRenderer3D, .toString=toString_gpu_cmd_impl_InitRenderer3D});
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="EGPUCMD_BeginFrameTile3D", .id=EGPUCMD_BeginFrameTile3D, .cmd=gpu_cmd_impl_BeginFrameTile3D, .validator=validate_gpu_cmd_impl_BeginFrameTile3D, .toString=toString_gpu_cmd_impl_BeginFrameTile3D});
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="EGPUCMD_SetShader3D", .id=EGPUCMD_SetShader3D, .cmd=gpu_cmd_impl_SetShader3D, .validator=validate_gpu_cmd_impl_SetShader3D, .toString=toString_gpu_cmd_impl_SetShader3D});
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="EGPUCMD_SetMatrix3D", .id=EGPUCMD_SetMatrix3D, .cmd=gpu_cmd_impl_SetMatrix3D, .validator=validate_gpu_cmd_impl_SetMatrix3D, .toString=toString_gpu_cmd_impl_SetMatrix3D});
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="EGPUCMD_DrawMesh3D", .id=EGPUCMD_DrawMesh3D, .cmd=gpu_cmd_impl_DrawMesh3D, .validator=validate_gpu_cmd_impl_DrawMesh3D, .toString=toString_gpu_cmd_impl_DrawMesh3D});        
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="EGPUCMD_DrawTriTex", .id=EGPUCMD_DrawTriTex, .cmd=gpu_cmd_impl_DrawTriTex, .validator=validate_gpu_cmd_impl_DrawTriTex, .toString=toString_gpu_cmd_impl_DrawTriTex});    
    gpu_register_cmd(state, (struct GpuCmdInfo_t){.name="EGPUCMD_LookAt3D", .id=EGPUCMD_LookAt3D, .cmd=gpu_cmd_impl_LookAt3D, .validator=validate_gpu_cmd_impl_LookAt3D, .toString=toString_gpu_cmd_impl_LookAt3D});    
}