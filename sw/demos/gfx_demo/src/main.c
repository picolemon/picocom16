#include "stdio.h"
#include "demo.h"
#include "picocom/devkit.h"

#define IMPL_EMBED_textures
#include "resources/textures.inl"
#define IMPL_EMBED_fonts
#include "resources/fonts.inl"
#define IMPL_EMBED_models
#include "resources/models.inl"

void app_main()
{
    // init sdk
    picocom_sleep_ms(500);

    PicocomInitOptions_t defaultOptions = picocom_get_default_init_options("gfx_demo");
    defaultOptions.resetDevices = 1;
    picocom_init_with_options(&defaultOptions);    

    demo_init();

    while(picocom_running()) 
    {
        demo_update();            
        picocom_update();        
    }
}
