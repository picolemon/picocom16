#pragma once

#include "platform.h"
#include "platform/pico/hw/picocom_hw.h"
#include "src/picocom/display/display.h"
#include "lib/platform/pico/boot/boot.h"

#ifdef __cplusplus
extern "C" {
#endif


/** Audio system init options
*/
typedef struct AudioOptions_t {
    int enabled;
    uint32_t streamBufferSize;
} AudioOptions_t;


/** Picocom global state.
*/
typedef struct PicocomGlobalState_t {
    int inited;
    const char* name;                       // App name        
    int hasDisplay;                         // Display valid
    int hasAudio;                           // Audio valid
    int hasInput;                           // Input valid
    int hasStorage;                         // Storage valid
    int defaultBusTimeout;                  // Default timeout
    const char* lastError;                  // Last api error
    uint32_t lastUpdateTime; 
    int running;                            // App running
} PicocomGlobalState_t;


/** Init sdk with options 
 */
typedef struct PicocomInitOptions_t
{    
    const char* name;   // App name
    int initAudio;      // Init audio driver
    int initDisplay;    // Init display driver
    int initStorage;    // Init storage driver
    int initInput;      // Init HID input driver
    int panicOnFailure; // Panic on sdk init, shows detailed internal errors if possible    
    int defaultBusTimeout; // Default timeout
    int showSplash;     // Show splash intro on sdk init
    int resetDevices;   // Reset devices
    int clockSpeedKhz;  // Init app clock in khz, zero to leave as default
    struct DisplayOptions_t displayOptions;   // Display init options
    struct AudioOptions_t audioOptions;     // Audio options    
} PicocomInitOptions_t;


// Main sdk
PicocomGlobalState_t* picocom_get_instance();   // Get global state, used by library functions to access chip links etc.
int picocom_init(const char* name);             // Init sdk, call just after main
PicocomInitOptions_t picocom_get_default_init_options(const char* name); // Get default options
int picocom_init_with_options(PicocomInitOptions_t* options);   // Custom sdk init
void picocom_panic(int errorCode, const char* message);         // Show a BSOD!
int picocom_message_box(const char* title, const char* msg);    // Show platform messagebox
int picocom_running();                  // App running
void picocom_log(const char* str);      // Debug log
void picocom_update();                  // Update sdk
void picocom_wdt();                     // Update wdt to show app is still working
void picocom_hw_setup_clocks(PicocomInitOptions_t* options);    // Hardware cpu clock config 
void picocom_hw_init(PicocomInitOptions_t* options);   // Hardware init
void picocom_hw_reset();                // Hardware reset
void picocom_hw_led_set(int state);     // Set onboard debug led for wdt indicator & boot flash

// utils
void picocom_sleep_us(uint32_t time);   // Sleep for uS
void picocom_sleep_ms(uint32_t time);   // Sleep for mS
uint32_t picocom_time_us_32();          // Get current time in uS
uint32_t picocom_time_ms_32();          // Get current time in ms
uint64_t picocom_time_us_64();          // Get current time in uS
uint64_t picocom_time_ms_64();          // Get current time in ms
typedef void (*Core1Callback_t)(void);
void picocom_multicore_launch_core1(Core1Callback_t entry);

// misc
bool stdio_init_all();

// Externs
extern unsigned char pUncomp[4096];

#ifdef __cplusplus
}
#endif
