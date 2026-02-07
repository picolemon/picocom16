#include "lib/gpu/gpu.h"
#include "picocom/devkit.h"
#include "gpu_types.h"
#include "stdio.h"
#include <tgx.h>
using namespace tgx;


// C-exports
extern "C" {
    void gpu_cmd_impl_InitRenderer3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
    void gpu_cmd_impl_BeginFrameTile3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
    void gpu_cmd_impl_SetShader3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
    void gpu_cmd_impl_SetMatrix3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
    void gpu_cmd_impl_DrawMesh3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
    void gpu_cmd_impl_DrawTriSolid_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
    void gpu_cmd_impl_DrawTriTex_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
    void gpu_cmd_impl_LookAt3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb);
}


/** Renderer state */
class GpuState3DImpl
{
public:
    static const Shader LOADED_SHADERS = SHADER_PERSPECTIVE | SHADER_ZBUFFER | SHADER_FLAT | SHADER_GOURAUD | SHADER_NOTEXTURE | SHADER_TEXTURE_NEAREST | SHADER_TEXTURE_WRAP_POW2;
    Renderer3D<RGB565, LOADED_SHADERS, uint16_t> renderer;
    uint16_t* zbuf;
    Image<RGB565> imfb;    
    tgx::Image<tgx::RGB565> images[GPU_MAX_BUFFER_ID];
};


//
//
void gpu_cmd_impl_InitRenderer3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
}


//
//
void gpu_cmd_impl_SetShader3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GpuState3DImpl* impl = (GpuState3DImpl*)gpu->globalState3D[job->instanceId];
    if( !impl )
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "!State3D");
        return;      
    }

    struct GPUCMD_SetShader3D* cmd = (GPUCMD_SetShader3D*)header;
    
    impl->renderer.setShaders( (tgx::Shader)cmd->shaderId );
}


//
//
void gpu_cmd_impl_SetMatrix3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GpuState3DImpl* impl = (GpuState3DImpl*)gpu->globalState3D[job->instanceId];
    if( !impl )
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "!State3D");
        return;      
    }

    struct GPUCMD_SetMatrix3D* cmd = (GPUCMD_SetMatrix3D*)header;
    fMat4 M;
    memcpy(M.M, cmd->M, sizeof(M.M));
    impl->renderer.setModelMatrix(M);
}

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
//
//
void gpu_cmd_impl_BeginFrameTile3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{    
    GpuState3DImpl* impl = (GpuState3DImpl*)gpu->globalState3D[job->instanceId];
    if( !impl )
    {
        impl = new GpuState3DImpl();
        impl->zbuf = new uint16_t[fb->w * fb->h];
        gpu->globalState3D[job->instanceId] = impl;
        impl->renderer.setViewportSize(FRAME_W, FRAME_H);    
        impl->renderer.setImage(&impl->imfb); // set the image to draw onto (ie the screen framebuffer)    
        impl->renderer.setZbuffer(impl->zbuf); // set the z buffer for depth testing        
        impl->renderer.setTextureQuality(SHADER_TEXTURE_NEAREST);
        impl->renderer.setTextureWrappingMode(SHADER_TEXTURE_WRAP_POW2);                 
    }

    struct GPUCMD_BeginFrameTile3D* cmd = (GPUCMD_BeginFrameTile3D*)header;
    
    // Fixup render state & clipping
    impl->imfb.set( fb->pixelsData, fb->w, fb->h );
    impl->renderer.setPerspective( cmd->fovy, cmd->aspect, cmd->zNear, cmd->zFar );

    // setup tile
    impl->renderer.setOffset(0, fb->y );    

    // clear buffers
    //impl->imfb.fillScreen( (fb->tileId % 2 == 0) ? (uint16_t)EColor16BPP_Red : (uint16_t)EColor16BPP_Blue/* cmd->fill*/  ); // Debug tiles
    impl->imfb.fillScreen( cmd->fill );

    if( cmd->clearZBuffer )
        impl->renderer.clearZbuffer();                    // clear the z-buffer

    if( cmd->clearAttrBuffer )
        for(int i=0;i<fb->w*fb->h;i++)
            fb->attr[i] = 0xff;
}



//
//
void gpu_cmd_impl_DrawMesh3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GpuState3DImpl* impl = (GpuState3DImpl*)gpu->globalState3D[job->instanceId];
    if( !impl )
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "!State3D");
        return;      
    }

    struct GPUCMD_DrawMesh3D* cmd = (GPUCMD_DrawMesh3D*)header;
    if(cmd->meshBufferId >= GPU_MAX_BUFFER_ID)
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "cmd->meshBufferId >= GPU_MAX_BUFFER_ID");
        return;
    }

    struct GpuBufferInfo* meshBuffer = &gpu->buffers[cmd->meshBufferId];
    struct GpuMesh3DBufferInfo* meshInfo = (GpuMesh3DBufferInfo*)meshBuffer->basePtr;
    if( !meshInfo )
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "!meshInfo");
        return;  
    }

    GpuBufferInfo* vertBuffer = gpu_get_buffer_by_id( gpu, meshInfo->vertsBufferId );
    if( !vertBuffer )
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "!vertBuffer");
        return;
    }    
    if( (vertBuffer->w * sizeof(float) * 3) != vertBuffer->size)       // Vec3 * numVerts
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "cmd->vertsBufferId invalid size");
        return;
    }

    GpuBufferInfo* texCoordsBuffer = gpu_get_buffer_by_id( gpu, meshInfo->texCoordsBufferId );
    if( !texCoordsBuffer )
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "!texCoordsBuffer");
        return;
    }    
    if( (texCoordsBuffer->w * sizeof(float) * 2) != texCoordsBuffer->size)       // Vec2* numTexCoords
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "cmd->texCoordsBufferId invalid size");
        return;
    }
    
    GpuBufferInfo* normalsBuffer = gpu_get_buffer_by_id( gpu, meshInfo->normalsBufferId );
    if( !normalsBuffer )
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "!normalsBuffer");
        return;
    }    
    if( (normalsBuffer->w * sizeof(float) * 3) != normalsBuffer->size)       // Vec2* numNormals
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "cmd->normalsBufferId invalid size");
        return;
    }
        
    GpuBufferInfo* facesBuffer = gpu_get_buffer_by_id( gpu, meshInfo->facesBufferId );
    if( !facesBuffer )
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "!facesBuffer");
        return;
    }    
    if( (facesBuffer->w * sizeof(uint16_t)) != facesBuffer->size)       // Vec2* numNormals
    {
        gpu_error(gpu, job, EGpuErrorCode_InvalidBufferId, "cmd->facesBufferId invalid size");
        return;
    }

    const tgx::Image<tgx::RGB565> img((void*)0, 0, 0);
    GpuBufferInfo* textureBuffer = gpu_get_buffer_by_id( gpu, cmd->textureBufferId );
    if( textureBuffer )
    {
        impl->images[ cmd->textureBufferId ] = tgx::Image<tgx::RGB565>((void*)textureBuffer->basePtr, textureBuffer->w, textureBuffer->h);
    }        
    
    // Build mesh
    tgx::Mesh3D<tgx::RGB565> mesh = {};        
    mesh.nb_vertices = vertBuffer->w;  
    mesh.nb_texcoords = texCoordsBuffer->w;
    mesh.nb_normals = normalsBuffer->w;
    mesh.nb_faces = facesBuffer->w; // Tri count ( uses 3 indices etc )
    mesh.len_face = facesBuffer->h; // Array size
    mesh.vertice = (const fVec3*)vertBuffer->basePtr;
    mesh.texcoord = (const fVec2*)texCoordsBuffer->basePtr;
    mesh.normal = (const fVec3*)normalsBuffer->basePtr;
    mesh.face = (uint16_t*)facesBuffer->basePtr;
    mesh.texture = impl->images[ cmd->textureBufferId ].isValid() ? &impl->images[ cmd->textureBufferId ] : nullptr;
    mesh.color = RGB565(1, 0, 1);
    mesh.ambiant_strength = 0.2f;
    mesh.diffuse_strength = 0.7f;
    mesh.specular_strength = 0.5f;
    mesh.specular_exponent = 32;
    mesh.bounding_box = {meshInfo->bounds[0], meshInfo->bounds[1], meshInfo->bounds[2], meshInfo->bounds[3], meshInfo->bounds[4], meshInfo->bounds[5] };
    
    // Draw params
    impl->renderer.setCulling( cmd->culling );    
    impl->renderer.setMaterial(RGBf(meshInfo->mat.r, meshInfo->mat.g, meshInfo->mat.b), meshInfo->mat.ambientStrength, meshInfo->mat.diffuseStrength, meshInfo->mat.specularStrength, meshInfo->mat.specularExponent );     

    impl->renderer.drawMesh( &mesh, false); 
}


//
//
void gpu_cmd_impl_DrawTriSolid_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GpuState3DImpl* impl = (GpuState3DImpl*)gpu->globalState3D[job->instanceId];
    if( !impl )
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "!State3D");
        return;      
    }

    struct GPUCMD_DrawTriSolid* cmd = (GPUCMD_DrawTriSolid*)header;

}


void gpu_cmd_impl_DrawTriTex_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{
    GpuState3DImpl* impl = (GpuState3DImpl*)gpu->globalState3D[job->instanceId];
    if( !impl )
    {
        gpu_error(gpu, job, EGpuErrorCode_General, "!State3D");
        return;      
    }

    struct GPUCMD_DrawTriTex* cmd = (GPUCMD_DrawTriTex*)header;
    
    // Draw params
    impl->renderer.setCulling( cmd->culling );    
    
    const tgx::Image<tgx::RGB565> img((void*)0, 0, 0);
    GpuBufferInfo* textureBuffer = gpu_get_buffer_by_id( gpu, cmd->textureBufferId );
    if( textureBuffer )
    {
        impl->images[ cmd->textureBufferId ] = tgx::Image<tgx::RGB565>((void*)textureBuffer->basePtr, textureBuffer->w, textureBuffer->h);
    }        
    
    fVec3 P1 = { cmd->P1x, cmd->P1y, cmd->P1z};
    fVec3 P2 = { cmd->P2x, cmd->P2y, cmd->P2z};
    fVec3 P3 = { cmd->P3x, cmd->P3y, cmd->P3z};
    fVec3 N1 = { cmd->N1x, cmd->N1y, cmd->N1z};
    fVec3 N2 = { cmd->N2x, cmd->N2y, cmd->N2z};
    fVec3 N3 = { cmd->N3x, cmd->N3y, cmd->N3z};
    fVec2 T1 = { cmd->T1x, cmd->T1y };
    fVec2 T2 = { cmd->T2x, cmd->T2y };
    fVec2 T3 = { cmd->T3x, cmd->T3y };

    impl->renderer.drawTriangle(P1, P2, P3,
                &N1, &N2, &N3,
                &T1, &T2, &T3,
                impl->images[ cmd->textureBufferId ].isValid() ? &impl->images[ cmd->textureBufferId ] : nullptr );
}


void gpu_cmd_impl_LookAt3D_impl(struct GpuState_t* gpu, struct GpuInstance_t* job, struct GpuCmd_Header* header, TileFrameBuffer_t* fb)
{   
 GpuState3DImpl* impl = (GpuState3DImpl*)gpu->globalState3D[job->instanceId];
    if( !impl )
    {
        impl = new GpuState3DImpl();
        impl->zbuf = new uint16_t[fb->w * fb->h];
        gpu->globalState3D[job->instanceId] = impl;
        impl->renderer.setViewportSize(FRAME_W, FRAME_H);    
        impl->renderer.setImage(&impl->imfb); // set the image to draw onto (ie the screen framebuffer)    
        impl->renderer.setZbuffer(impl->zbuf); // set the z buffer for depth testing        
        impl->renderer.setTextureQuality(SHADER_TEXTURE_NEAREST);
        impl->renderer.setTextureWrappingMode(SHADER_TEXTURE_WRAP_POW2);                 
    }

    struct GPUCMD_LookAt3D* cmd = (GPUCMD_LookAt3D*)header;
    
    fVec3 eye = { cmd->eyeX, cmd->eyeY, cmd->eyeZ};
    fVec3 center = { cmd->centerX, cmd->centerY, cmd->centerZ};
    fVec3 up = { cmd->upX, cmd->upY, cmd->upZ};

    impl->renderer.setLookAt( eye, center, up);
}