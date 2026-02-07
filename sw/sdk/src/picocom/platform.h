#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Build defines ( Set by cmakefiles )
//#define PICOCOM_SDL           // [sim] Set for SDL simulator, emulates each core on a thread and full pico bus async emulation.
//#define PICOCOM_NATIVE_SIM    // [sim] Set for emscripten or with SDL simulator, single core inline pico bus calls.

// malloc enforce
#ifdef PICOCOM_MALLOC_ENFORCE
    #ifndef PICOCOM_MALLOC_IMPL
        #define malloc(x) __use_picocom_malloc()
        #define free(x) __use_picocom_free()
        #define realloc(x,y) __use_picocom_realloc()
    #endif
#endif

// picocom malloc
void* picocom_malloc(size_t sz);
void picocom_free(void* ptr);
void* picocom_relloc(void * ptr, size_t size);

// common macros
#ifndef MIN
    #define MIN(a,b) (((a)<(b))?(a):(b))
    #define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#ifdef PICO_BOARD
    #define PICOCOM_PICO 
    #include "pico/stdlib.h"
#else
    #define PICOCOM_SDL
    #define __in_flash()
    void tight_loop_contents();
#endif

#ifdef __GNUC__
    #define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
    #define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

#ifdef PICOCOM_SDL
    #ifndef __not_in_flash_func
        #define __not_in_flash_func(func_name) func_name
    #endif
#else
    #ifndef __not_in_flash_func
            #define __not_in_flash_func(func_name) __not_in_flash(__STRING(func_name)) func_name
    #endif
#endif

// Macros
#define NUM_ELEMS(a) (sizeof(a) / sizeof 0[a])
#define ZERO_MEM(a) memset(&a, 0, sizeof(a));
#define ZERO_MEM_PTR(a) memset(a, 0, sizeof(*a));

/** Common codes
 */
enum SDKErr
{
    SDKErr_OK = 0,
    SDKErr_Fail = -1,    // A general can't be bothered to specify error
};

// Low level profiler
void profiler_begin(const char* section);
void profiler_end(const char* section);
void profiler_dump();
