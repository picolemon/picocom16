/** Light simulation layer for the pico_audio library
*/
#pragma once

#include "picocom/platform.h"
#include <SDL2/SDL.h>
#include "picocom/utils/array.h"
#ifdef PICOCOM_SDL
    #include "lib/components/mock_hardware/queue.h"
    #include "lib/components/mock_hardware/mutex.h"
#else
    #include "pico/util/queue.h"
    #include "pico/mutex.h"
#endif

/* Picocm16 compat audio format */
#define AUDIO_FORMAT AUDIO_S16LSB
#define AUDIO_FREQUENCY 11025
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES 4096
#define AUDIO_MAX_SOUNDS 25
#define AUDIO_MUSIC_FADE_VALUE 2

#define AUDIO_BUFFER_FORMAT_PCM_S16 1          ///< signed 16bit PCM
#define AUDIO_BUFFER_FORMAT_PCM_S8 2           ///< signed 8bit PCM
#define AUDIO_BUFFER_FORMAT_PCM_U16 3          ///< unsigned 16bit PCM
#define AUDIO_BUFFER_FORMAT_PCM_U8 4           ///< unsigned 8bit PCM


/** buffer */
typedef struct mem_buffer {
    size_t size;
    uint8_t *bytes;
    uint8_t flags;
} mem_buffer_t;


/** Audio format */
typedef struct audio_format {
    uint32_t sample_freq;      // sample freq hz
    uint16_t format;           // audio format
    uint16_t channel_count;    // channel count
} audio_format_t;


/** Audio buffer format definition */
typedef struct audio_buffer_format {
    const audio_format_t *format;           // format ref
    uint16_t sample_stride;                 // per sample stride in bytes
} audio_buffer_format_t;


/** Audio buffer  */
typedef struct audio_buffer {
    mem_buffer_t *buffer;
    const audio_buffer_format_t *format;
    uint32_t sample_count;
    uint32_t max_sample_count;
    uint32_t user_data;     
    uint32_t buffer_alloc_size;
    uint32_t queue_time;
} audio_buffer_t;


/** Audio pool */
typedef struct audio_buffer_pool {
    SDL_AudioSpec audio;    
    SDL_AudioSpec want;
    SDL_AudioSpec got;
    SDL_AudioDeviceID device;
    uint8_t audioEnabled;    
    audio_buffer_t*audio_buffers;
    queue_t freePool;
    queue_t outputPool;
    uint32_t currentBufferReadPos;
    audio_buffer_t *currentBuffer;                
    uint32_t starveCnt;
} audio_buffer_pool_t;


// audio api
struct audio_buffer_pool* audio_sdl2_connect( audio_buffer_format_t *format, int buffer_count, int buffer_sample_count );
void audio_sdl2_disconnect(struct audio_buffer_pool* ac);
audio_buffer_t *take_audio_buffer(audio_buffer_pool_t *ac, bool block);
void give_audio_buffer(audio_buffer_pool_t *ac, audio_buffer_t *buffer);
void sdl_audio_disable_audio();