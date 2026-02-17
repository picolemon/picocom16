/** SDL Platform core launcher
 * - create mock picobus connections between core ( emulate PIO bus iterface at packet level )
 * - create each core on picocom16 hw board eg. vdp1, vdp2, apu and user app core
 */
#pragma  once

#ifdef __linux__
    #include <SDL2/SDL.h>
#elif _WIN32
    #include <SDL.h>
#elif __EMSCRIPTEN__    
    #include <SDL2/SDL.h>    
#endif
