#pragma once

#include "picocom/platform.h"
#include "picocom/audio/audio.h"
#include "gpu/gpu.h"
#include "platform/pico/bus/bus.h"
#include "platform/pico/apu/hw_apu_types.h"
#ifdef PICOCOM_SDL
#include "lib/components/mock_hardware/mutex.h"
#else
#include <pico/mutex.h>
#endif
#ifdef PICOCOM_NATIVE_SIM
    #include <dirent.h>
    #include <sys/stat.h>  
    #include <stdio.h>
    #include <stdlib.h>
#else
    #include "ff.h"
#endif

// Config
#define APU_MAX_OPEN_FP 32

// Fwd
struct AudioEngineState_t;


/** APU init options */
typedef struct apuInitOptions_t
{
    BusTx_t* app_alnk_tx;
    BusRx_t* app_alnk_rx;    
} apuOptions_t;


/** APU run options */
typedef struct apuMainLoopOptions_t
{
    bool audioRenderOnly;
} apuMainLoopOptions_t;


/** File pointer cache */
typedef struct apuFPcache_t
{
    bool isValid;
    char filename[256];  
    uint64_t fileHandleId;
    uint32_t size;        
#ifndef PICOCOM_NATIVE_SIM    
    FIL fil;
    DIR dir;
#endif    
#ifdef PICOCOM_NATIVE_SIM   
    DIR *dir;
    FILE *fil;
#endif
    bool isDir;
} apuFPcache_t;


/** APU state */
typedef struct apu_t
{    
    BusTx_t* app_alnk_tx;             // APU -> APP (out) bus
    BusRx_t* app_alnk_rx;             // APP -> APU (in) bus
    struct AudioEngineState_t* audio; // Audio engine state
    uint32_t lastAutoMountCheckTime; // SD mount interval
    bool sdDriverInited;                // SD SPI driver inited
    bool sdCardDetected;             // SD card was detected in slow
    bool sdCardMounted;             // SD card was mounted    
    bool sdCardPendingRestartOnDiskErr; // IO read disk error ( usually removed disk ), sd driver needs restarting
    struct apuFPcache_t fpCache[APU_MAX_OPEN_FP];  // Open files
#ifndef PICOCOM_NATIVE_SIM    
    FATFS fs;                       // Fat fs instance
#endif    
    uint32_t resetCount;
    uint32_t startupTime;
    bool triggeredErrorSfx;
    mutex_t bufferWriteLock;
    uint16_t lastAsyncIOTxSeqNum;
} apu_t;


// apu core
int apu_init(struct apu_t* apu, struct apuInitOptions_t* options);
int apu_deinit(struct apu_t* apu);
#ifdef PICOCOM_NATIVE_SIM    
void apu_update(struct apu_t* apu, struct apuMainLoopOptions_t* options);
#else
int apu_main(struct apu_t* apu, struct apuMainLoopOptions_t* options);
#endif

// internal
void apu_clear_fp_cache(struct apu_t* apu);     // Clear open file cache
struct apuFPcache_t* apu_find_fp_by_filename(struct apu_t* apu, const char* filename); // search open files
struct apuFPcache_t* apu_find_fp_by_handleId(struct apu_t* apu, uint64_t handleId); // get by handle
struct apuFPcache_t* apu_open_file(struct apu_t* apu, const char* filename, int mode, uint32_t* errorCodeOut); // do fp open and alloc cache ( files of same name will not be de-duplicated )
struct apuFPcache_t* apu_open_dir(struct apu_t* apu, const char* filename, uint32_t* errorCodeOut); // do fp open and alloc cache 
bool apu_close_file(struct apu_t* apu, struct apuFPcache_t* fpCache); // Close fp handle
void autoMountSDCard(struct apu_t* apu);
void apu_play_error_sfx(struct apu_t* apu);