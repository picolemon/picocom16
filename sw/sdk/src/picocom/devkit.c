#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include "devkit.h"
#include "picocom/input/input.h"
#include "picocom/audio/audio.h"
#include "picocom/display/display.h"
#include "picocom/display/gfx.h"
#include "picocom/storage/storage.h"
#include "string.h"
#ifdef PICOCOM_PICO
#include "pico/stdlib.h"
#include "platform/pico/hw/picocom_hw.h"
#include "platform/pico/boot/hw_boot_types.h"
#endif


//
// Globals
static PicocomGlobalState_t * g_SDKState = 0;
static uint32_t wdt_counter = 0;
static uint32_t last_wdt_time = 0;
unsigned char pUncomp[4096];            // zlib compression buffer


//
//
static int internal_picocom_init_with_options(PicocomInitOptions_t* options)
{
    int res;

#ifdef PICOCOM_PICO
    // Reset cores on init & bootloaders
    if(options->resetDevices)
    {
        // Setup reset before floating bootsel triggers
        gpio_init(APP_DEVICE_RESET_PIN);
        gpio_put(APP_DEVICE_RESET_PIN, 1);
        gpio_set_dir(APP_DEVICE_RESET_PIN, GPIO_OUT);         
    }
#endif

    if(g_SDKState && g_SDKState->inited)
        return 0; // Already inited

    picocom_hw_setup_clocks(options);

    g_SDKState = picocom_malloc(sizeof(PicocomGlobalState_t));
    memset(g_SDKState, 0, sizeof(PicocomGlobalState_t));
    g_SDKState->defaultBusTimeout = options->defaultBusTimeout;
    g_SDKState->lastError = 0;
    g_SDKState->name = options->name;
    g_SDKState->running = 1;

    picocom_sleep_ms(100);

    // reset devices
    picocom_hw_init(options);    
    picocom_sleep_ms(100);

#ifdef PICOCOM_PICO
    // Reset cores on init & bootloaders
    if(options->resetDevices)
    {
		// hardware reset to bootloader
		picocom_hw_reset();

        // NOTE: Bootloader paused for stability, stable roms with fixed function by default also this
        // is insanely complex and could have a high failure rate.
		// wait for core to boot 
		/*sleep_ms(BOOT_WAIT_BOOTLOADER_TIME);

        // Run core bootloaders after boot
        // NOTE: Either all cores or none
        if(options->bootCores)
        {
		    // lock bootloader on selected cores
		    struct BootClient_t clientVDP12Group;
		    struct BootClient_t clientAPU;
		    res = bootclient_init(&clientVDP12Group, EBootTargetCore_VDP1, true);
		    if(res != SDKErr_OK)		
    			picocom_panic(res, "clientVDP12Group init failed");		
		    res = bootclient_init(&clientAPU, EBootTargetCore_APU, true);
		    if(res != SDKErr_OK)		
    			picocom_panic(res, "clientAPU init failed");		
				
            // boot core
            res = bootclient_boot( &clientVDP12Group, false );
            if(res != SDKErr_OK)		
                picocom_panic(res, "client boot failed");

            // boot core
            res = bootclient_boot( &clientAPU, false );
            if(res != SDKErr_OK)		
                picocom_panic(res, "client boot failed");  

            // boot core
            res = bootclient_boot( &clientAPU, true );
            if(res != SDKErr_OK)		
                picocom_panic(res, "client boot failed");    

            sleep_ms(BOOT_WAIT_APPSTART_TIME);                 
        }*/
    }
#endif

    // Init display driver
    if(options->initDisplay)
    {
        res = display_init_with_options(&options->displayOptions);
        if(res != SDKErr_OK)
        {
            if(!g_SDKState->lastError)
                g_SDKState->lastError = "Failed to init display api";            
            return res;        
        }
        g_SDKState->hasDisplay = 1;
    }

    picocom_sleep_ms(100);

    // Init audio driver
    if(options->initAudio)
    {        
        res = audio_init_with_options(&options->audioOptions);
        if(res != SDKErr_OK)
        {
            if(!g_SDKState->lastError)
                g_SDKState->lastError = "Failed to init audio api";
            return res;      
        }

        picocom_sleep_ms(250);

        res = audio_reset_apu();
        if(res != SDKErr_OK)
        {
            if(!g_SDKState->lastError)
                g_SDKState->lastError = "Apu reset failed";
            return res;      
        }

        g_SDKState->hasAudio = 1;  
    }
    
    picocom_sleep_ms(100);    
    
    // Init input driver (dep APU)
    if(options->initInput&& options->initAudio)
    {
        struct InputInitOptions_t inputOptions = input_default_options(audio_get_impl());
        res = input_init_with_options(&inputOptions);
        if(res != SDKErr_OK)
        {
            if(!g_SDKState->lastError)
                g_SDKState->lastError = "Failed to init input api";
            return res;       
        }
        g_SDKState->hasInput = 1;   
    }

    picocom_sleep_ms(100);
    
    // Init storage driver (dep APU)
    if(options->initStorage && options->initAudio)
    {
        struct StorageOptions_t storageOptions = storage_default_options(audio_get_impl());
        res = storage_init_with_options(&storageOptions);
        if(res != SDKErr_OK)
        {
            if(!g_SDKState->lastError)
                g_SDKState->lastError = "Failed to storage audio api";
            return res;        
        }
        g_SDKState->hasStorage = 1;   
    }

    g_SDKState->inited = 1;    

    return SDKErr_OK;
}


int picocom_init_with_options(PicocomInitOptions_t* options)
{
    int ret = internal_picocom_init_with_options(options);
    if(ret != SDKErr_OK)
    {
        // Panic handler so sdk can just single line picocom_init()
        if(options->panicOnFailure)
        {
            picocom_panic(ret, NULL);
        }
    }

    // delay for bus to work, settle io after boot as signals will be in an undefined state and may trigger cs lines.
    picocom_sleep_ms(100);

    return ret;
}


PicocomInitOptions_t picocom_get_default_init_options(const char* name)
{
    PicocomInitOptions_t defaultOptions = {
        .name = name,
        .initAudio = 1,
        .audioOptions = audio_default_options(),
        .initDisplay = 1,
        .displayOptions = display_default_options(),
        .initStorage = 1,
        .initInput = 1,
        .panicOnFailure = 1,
        .defaultBusTimeout = 1000,
        .showSplash = 1,
        .resetDevices = 1,
#ifdef PICOCOM_PICO        
        .clockSpeedKhz = APP_CLOCK_KHZ,
#endif
    };
    return defaultOptions;
}


int picocom_init(const char* name)
{
    PicocomInitOptions_t defaultOptions = picocom_get_default_init_options(name);
    return picocom_init_with_options(&defaultOptions);
}


PicocomGlobalState_t* picocom_get_instance()
{
    return g_SDKState;
}


void picocom_log(const char* str)
{
    if(!str)
        return;
    printf("[INFO] %s\n", str);
}


void picocom_panic(int errorCode, const char* message)
{
    // Grab last error if not set by caller
    if(!message && g_SDKState)
        message = g_SDKState->lastError;

    printf("[picocom_panic] errorCode: %d, message: %s\n", errorCode, message ? message : "NULL");
    bool displayedMessage = false;
    if(g_SDKState)
    {        
        // Display message if display
        if( g_SDKState->hasDisplay )
        {

        }
    }

    // Message confirm
    int buttonId = picocom_message_box("Panic", message);

    if( buttonId == 1 )
    {
        // Continue
        return;
    }


#ifdef PICOCOM_PICO

    // Force display driver init
    if(!displayedMessage && g_SDKState && !g_SDKState->hasDisplay )
    {
        struct DisplayOptions_t displayOptions = display_default_options();
        int res = display_init_with_options(&displayOptions);        
    }

    // Panic loop        
    while(1)
    {
        picocom_sleep_ms(100);                
        picocom_hw_led_set(1);        
        picocom_sleep_ms(100);                
        picocom_hw_led_set(0);        

        picocom_sleep_ms(100);                
        picocom_hw_led_set(1);        
        picocom_sleep_ms(100);                
        picocom_hw_led_set(0);        

        picocom_sleep_ms(1000);        

        // Red screen of death
        if(!displayedMessage)
        {
            gfx_begin_frame();
            gfx_fill_rect( 0, 0, FRAME_W, FRAME_H, EColor16BPP_Red, 0xff );
            gfx_end_frame();                
        }
    }    
#endif    
    exit(1);
}


void picocom_update()
{
    if(!g_SDKState)
        return;

    // update display
    if(g_SDKState->hasDisplay)
        display_update();                

    // update input
    if(g_SDKState->hasInput)
        input_update(); 

    // update audio
    if(g_SDKState->hasAudio)
        audio_update();  

    // update io
    if(g_SDKState->hasStorage)
        storage_update();  

    // wtd ping / indicator
    g_SDKState->lastUpdateTime = picocom_time_us_32();
    picocom_wdt();
}


void picocom_wdt()
{
    if(picocom_time_us_32() - last_wdt_time > 1000000)
    {
        wdt_counter++;

        static int ledState = 1;
        picocom_hw_led_set(ledState);
        ledState = !ledState;

        last_wdt_time = picocom_time_us_32();
    }
}


int picocom_running()
{
    if(!g_SDKState)
        return 0;

    return g_SDKState->running;
}


#ifndef PICOCOM_OVERRIDE_MAIN
#ifndef PICOCOM_SDL
void app_main();
int main()
{
    app_main();
    return 0;
}
#endif
#endif // PICOCOM_OVERRIDE_MAIN