#pragma once

#include "picocom/platform.h"
#include "picocom/display/display.h"
#include "platform/sdl2/sdl_platform.h"


#ifdef __cplusplus
extern "C" {
#endif


/** SDL display driver 
*/
typedef struct SDLDisplayDriverState_t
{    
    // Sdl
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_AudioDeviceID device;
    SDL_AudioSpec want;    

    // screen buffer    
    uint32_t currentBufferId;
    uint16_t* screenBuffers[2];

    SDL_Surface* screenSurface;
    uint16_t* tileDepth;    
    int gpuCmdsDirty;

    uint32_t frameTime;
    uint32_t lastFlipTime;

} SDLDisplayDriverState_t;

// sdl api
void sdl_display_set_window_scale(uint32_t simWindowScale);
uint32_t sdl_display_get_window_scale();
void sdl_display_set_window_title(const char* simTitle);
const char* sdl_display_get_window_title();
void sdl_display_set_window_dummy_event_loop(int enabled);         // tick events on frameflip (no input simulated)
void sdl_display_set_fullscreen(bool fullscreen);

#ifdef __cplusplus
}
#endif
