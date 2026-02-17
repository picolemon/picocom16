#include "sdl_display_driver.h"
#include "picocom/devkit.h"
#include "picocom/display/display.h"
#include "gpu/gpu.h"


// fwd
void hid_update_sdl_state();


// Globals
struct SDLDisplayDriverState_t* sdlDisplayState = 0;
bool sdlDisplayStateValid = false;
uint32_t sdlSimWindowScale = 1;
bool sdlSimulateFpsLimit = true ;   // Disabled on PICOCOM_NATIVE_SIM
const char* sdlSimWindowTitle = "Picocom16 app";
uint32_t sdlSimWindowDummyEventLoop = 0;


//
//
static double rng(double min, double max)
{
    return (double)rand()/(double)RAND_MAX * (max - min) + min;
}


bool display_driver_get_init()
{
    return sdlDisplayState != 0;
}

bool display_driver_init()
{
    if(sdlDisplayState)
        return 0;
    
    sdlDisplayState = (SDLDisplayDriverState_t*)malloc(sizeof(SDLDisplayDriverState_t));
    if(!sdlDisplayState)
        return false;
    memset(sdlDisplayState, 0, sizeof(SDLDisplayDriverState_t));    
    if(SDL_Init( SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER ) < 0)
    {        
        picocom_panic(SDKErr_Fail, "SDL init failed");
    }
    else 
    {        
        sdlDisplayState->window = SDL_CreateWindow(
            sdlSimWindowTitle, 
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            FRAME_W * sdlSimWindowScale, FRAME_H * sdlSimWindowScale,
            SDL_WINDOW_SHOWN
        );

        if(!sdlDisplayState->window)
            picocom_panic(SDKErr_Fail, "SDL window init failed"); 
        
        SDL_Delay(100);
    }

    sdlDisplayState->renderer = SDL_CreateRenderer(sdlDisplayState->window, -1, SDL_RENDERER_SOFTWARE);
    if(!sdlDisplayState->renderer)
        picocom_panic(SDKErr_Fail, "SDL renderer init failed"); 

    SDL_RenderSetScale(sdlDisplayState->renderer, sdlSimWindowScale, sdlSimWindowScale);

    sdlDisplayState->screenSurface = SDL_CreateRGBSurface(0, FRAME_W*sdlSimWindowScale, FRAME_H*sdlSimWindowScale, 32, 0, 0, 0, 0);
    if(!sdlDisplayState->screenSurface)
        picocom_panic(SDKErr_Fail, "SDL failed to get back buffer surface"); 

    SDL_Surface* screen = SDL_GetWindowSurface(sdlDisplayState->window);
    if(!screen)
        picocom_panic(SDKErr_Fail, "SDL failed to get window surface"); 

    sdlDisplayState->screenBuffers[0] = malloc(FRAME_W*FRAME_H*sizeof(uint16_t));
    sdlDisplayState->screenBuffers[1] = malloc(FRAME_W*FRAME_H*sizeof(uint16_t));
    if(!sdlDisplayState->screenBuffers[0] || !sdlDisplayState->screenBuffers[0])
        picocom_panic(SDKErr_Fail, "Failed to alloc frame buffers"); 

    for(int i=0;i<FRAME_W*FRAME_H;i++)
    {
        sdlDisplayState->screenBuffers[0][i] = (int)rng(0,0xffff);
        sdlDisplayState->screenBuffers[1][i] = (int)rng(0,0xffff);
        sdlDisplayState->screenBuffers[0][i] = 0;
        sdlDisplayState->screenBuffers[1][i] = 0xffff;
    }

    sdlDisplayState->frameTime = 1000000 / 69;

    // hide cursor by default to emulate hw
    SDL_ShowCursor(0);

    sdlDisplayStateValid = true;
    return true;    
}

void display_driver_deinit()
{
    if(sdlDisplayState)
        return;
        
    SDL_Quit();

    free(sdlDisplayState);
    sdlDisplayState = 0;
}


uint16_t* get_display_buffer()
{
    if(!sdlDisplayState)
        return 0;

    int index = (sdlDisplayState->currentBufferId + 0) % 2;
    uint16_t* screenBuffer = sdlDisplayState->screenBuffers[index];
    return screenBuffer;
}

void display_buffer_copy_front()
{
    if(!sdlDisplayState)
        return;
    
    // get back
    int index = (sdlDisplayState->currentBufferId + 0) % 2;
    uint16_t* screenBuffer = sdlDisplayState->screenBuffers[index];

    // get screen FB
    uint32_t* dstFb = (uint32_t*) sdlDisplayState->screenSurface->pixels;
    if(!dstFb){		
        return;
    }

    // Upscale in an absolute horible way
    uint32_t dstPitch = sdlDisplayState->screenSurface->pitch / sizeof(uint32_t);    
    for(int y=0;y<FRAME_H;y++)
    {   
        for(int j=0;j<sdlSimWindowScale;j++)    // line repeat 
            for(int x=0;x<FRAME_W;x++)
            {
                struct ColRGBF_t rgbf = gpu_col_to_rgbf(gpu_col_from_uint16(screenBuffer[x + (y*FRAME_W)]));            
                uint32_t c = (((uint8_t)(rgbf.r * 255) & 0xff) << 16)
                    | (((uint8_t)(rgbf.g * 255) & 0xff) << 8)
                    | (((uint8_t)(rgbf.b * 255) & 0xff) << 0);

                int dstIndex = 0;

                for(int i=0;i<sdlSimWindowScale;i++)
                    dstFb[ ((x*sdlSimWindowScale)+i) + (((y*sdlSimWindowScale)+j) * dstPitch)] = c;
            }
    }

    // Copy small fb to desktop window, size to fit
    SDL_Surface* screen = SDL_GetWindowSurface(sdlDisplayState->window);
    if (!screen) {		
        return;
    }

    struct SDL_Rect destRect = (struct SDL_Rect){.x = 0, 
        .y=0, 
        .w=FRAME_W*sdlSimWindowScale, 
        .h=FRAME_H*sdlSimWindowScale
    };
    SDL_BlitSurface(sdlDisplayState->screenSurface, NULL, screen, &destRect);    

    // flip sim 
    SDL_UpdateWindowSurface(sdlDisplayState->window);
    SDL_RenderPresent(sdlDisplayState->renderer);

    // dummy sdl window update
    if(sdlSimWindowDummyEventLoop)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch (event.type) {
            }
        }
    }
}


uint16_t* flip_display_blocking()
{
    if(!sdlDisplayState)
        return 0;
    
    // get back
    int index = (sdlDisplayState->currentBufferId + 0) % 2;
    uint16_t* screenBuffer = sdlDisplayState->screenBuffers[index];

    // flip
    sdlDisplayState->currentBufferId++;

    // get screen FB
    uint32_t* dstFb = (uint32_t*) sdlDisplayState->screenSurface->pixels;
    if(!dstFb){		
        return 0;
    }

    // Upscale in an absolute horible way
    uint32_t dstPitch = sdlDisplayState->screenSurface->pitch / sizeof(uint32_t);    
    for(int y=0;y<FRAME_H;y++)
    {   
        for(int j=0;j<sdlSimWindowScale;j++)    // line repeat 
            for(int x=0;x<FRAME_W;x++)
            {
                struct ColRGBF_t rgbf = gpu_col_to_rgbf(gpu_col_from_uint16(screenBuffer[x + (y*FRAME_W)]));            
                uint32_t c = (((uint8_t)(rgbf.r * 255) & 0xff) << 16)
                    | (((uint8_t)(rgbf.g * 255) & 0xff) << 8)
                    | (((uint8_t)(rgbf.b * 255) & 0xff) << 0);

                int dstIndex = 0;

                for(int i=0;i<sdlSimWindowScale;i++)
                    dstFb[ ((x*sdlSimWindowScale)+i) + (((y*sdlSimWindowScale)+j) * dstPitch)] = c;
            }
    }

    // Copy small fb to desktop window, size to fit
    SDL_Surface* screen = SDL_GetWindowSurface(sdlDisplayState->window);
    if (!screen) {		
        return 0;
    }

    struct SDL_Rect destRect = (struct SDL_Rect){.x = 0, 
        .y=0, 
        .w=FRAME_W*sdlSimWindowScale, 
        .h=FRAME_H*sdlSimWindowScale
    };
    SDL_BlitSurface(sdlDisplayState->screenSurface, NULL, screen, &destRect);    

    // flip sim 
    SDL_UpdateWindowSurface(sdlDisplayState->window);
    SDL_RenderPresent(sdlDisplayState->renderer);

    // dummy sdl window update
    if(sdlSimWindowDummyEventLoop)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch (event.type) {
            }
        }
    }
    
    // display sync 60hz
    uint32_t dt = picocom_time_us_32() - sdlDisplayState->lastFlipTime;
    if(dt == sdlDisplayState->lastFlipTime)
        dt = 0;
    sdlDisplayState->lastFlipTime = picocom_time_us_32();

    int32_t remain = sdlDisplayState->frameTime - dt;
    if(remain > sdlDisplayState->frameTime)
        remain = sdlDisplayState->frameTime;
    
    // Free fps on native, sdl sim will lock fps
#ifndef PICOCOM_NATIVE_SIM    
    if(sdlSimulateFpsLimit && remain > 0)
        picocom_sleep_us(remain);
#endif        
    
    // get current / new
    return get_display_buffer();    
}


//
//
void sdl_display_set_window_scale(uint32_t simWindowScale)
{
    sdlSimWindowScale = simWindowScale;
}


uint32_t sdl_display_get_window_scale()
{
    return sdlSimWindowScale;
}


void sdl_display_set_window_title(const char* simTitle)
{
    sdlSimWindowTitle = simTitle;
}


const char* sdl_display_get_window_title()
{
    return sdlSimWindowTitle;
}


void sdl_display_set_window_dummy_event_loop(int enabled)
{
    sdlSimWindowDummyEventLoop = enabled;
}


void sdl_display_set_fullscreen(bool fullscreen)
{
    if(fullscreen) {
        SDL_SetWindowFullscreen(sdlDisplayState->window, 0);
    } else {
        SDL_SetWindowFullscreen(sdlDisplayState->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
}
