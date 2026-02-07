#pragma once

#include "picocom/devkit.h"
#include "picocom/display/display.h"
#include "picocom/display/gfx.h"
#include "picocom/input/input.h"
#include "gpu/gpu.h"


// Fwd
struct DemoState;
struct DemoInfo;

// Config
#define DEMO_INIT_DEMO_ID EDemoId_FillRect

// Callbacks
typedef void* (*DemoInit_t)(struct DemoState* demo, struct DemoInfo* info);
typedef void (*DemoUpdate_t)(struct DemoState* demo, struct DemoInfo* info, void * instance);
typedef void (*DemoFree_t)(struct DemoState* demo, struct DemoInfo* info, void * instance);


/** Demo types */
enum EDemoId
{    
    EDemoId_DrawLine,       // Draw line
    EDemoId_FillRect,       // Solid fill
    EDemoId_Blit,           // Blit image
    EDemoId_DrawText,       // Draw text
    EDemoId_RadialGrad,     // Custom shader
    EDemoId_TrexDrawMesh3D, // TRex draw mesh 3d
    EDemoId_Max
};


/* Demo info */
typedef struct DemoInfo {
    const char* name;
    DemoInit_t init;
    DemoUpdate_t update;
    DemoFree_t free;
} DemoInfo;


/* Demo state */
typedef struct DemoState {
    int demoId;
    struct DemoInfo demos[EDemoId_Max];
    void* demoInstance;
    uint32_t fontResourceId;
    uint32_t fontTextureId;
    uint32_t showDemoNameTime;  // demo change overlay
    bool drawFps;
    // shared state
    int state0;
} DemoState;


// demo api
void demo_init();
void demo_update();
void demo_set_index(uint32_t i);
void demo_render_overlay(); // render demo overlay in frame (called by demo impl)
void demo_default_free(struct DemoState* demo, struct DemoInfo* info, void * instance);
