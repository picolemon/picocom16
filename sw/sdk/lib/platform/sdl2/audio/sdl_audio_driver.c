#include "picocom/devkit.h"
#include "picocom/audio/audio.h"
#include "sdl_audio_driver.h"

// Fwd
void test_core_apu_render_audio();

// Native uses sdl2 for portability where the core sim uses alsa to resolve sdl2 lag issues on linux, will make these preproc options at some point when cmake behave's itself.
#ifdef PICOCOM_NATIVE_SIM 

// Externs
bool g_AudioThreadEnabled = false;
extern bool sdlDisplayStateValid;

float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}


//
//
static inline void audioCallback(void * userdata, uint8_t* streamOutPtr, int len)
{
    struct audio_buffer_pool* ac = (struct audio_buffer_pool*)userdata;

    // Handles limited a number of audio formats
    if(ac->got.format == AUDIO_FORMAT && ac->got.freq == AUDIO_FREQUENCY )    // Exact spec
    {        
        int16_t* streamOut = (int16_t*)streamOutPtr;

        uint32_t writtenBytes = 0;
        while(writtenBytes < len / sizeof(int16_t))
        {
            // pop next buffer
            if(!ac->currentBuffer)
            {            
                while(!queue_try_remove(&ac->outputPool, &ac->currentBuffer))
                {
                    test_core_apu_render_audio();
                    ac->starveCnt++;
                }
                ac->currentBufferReadPos = 0;
            }

            // write buffer bytes        
            int16_t* streamIn = (int16_t*)ac->currentBuffer->buffer->bytes;
            for(int i=ac->currentBufferReadPos;i<ac->currentBuffer->buffer->size / sizeof(int16_t);i++)
            {
                streamOut[writtenBytes++] = streamIn[ac->currentBufferReadPos++];
                if(writtenBytes >= len)
                    break;        
            }

            // pop current
            if(ac->currentBufferReadPos >= ac->currentBuffer->buffer->size/ sizeof(int16_t))
            {
                while(!queue_try_add(&ac->freePool, &ac->currentBuffer))
                {                
                    tight_loop_contents();
                    picocom_sleep_us(100);
                }
                ac->currentBuffer = 0;
                ac->currentBufferReadPos = 0;
            }
        }
    }
    else if(ac->got.format == AUDIO_F32LSB && ac->got.freq == AUDIO_FREQUENCY )   // 11k with 32bit
    {
        float* streamOut = (float*)streamOutPtr;

        uint32_t writtenBytes = 0;
        while(writtenBytes < len / sizeof(float))
        {
            // pop next buffer
            if(!ac->currentBuffer)
            {            
                while(!queue_try_remove(&ac->outputPool, &ac->currentBuffer))
                {
                    test_core_apu_render_audio();
                    ac->starveCnt++;
                }
                ac->currentBufferReadPos = 0;
            }

            // write buffer bytes        
            int16_t* streamIn = (int16_t*)ac->currentBuffer->buffer->bytes;
            for(int i=ac->currentBufferReadPos;i<ac->currentBuffer->buffer->size / sizeof(int16_t);i++)
            {
                streamOut[writtenBytes++] = (streamIn[ac->currentBufferReadPos++]) / 32767.5f;
                if(writtenBytes >= len)
                    break;        
            }

            // pop current
            if(ac->currentBufferReadPos >= ac->currentBuffer->buffer->size/ sizeof(int16_t))
            {
                while(!queue_try_add(&ac->freePool, &ac->currentBuffer))
                {                
                    tight_loop_contents();
                    picocom_sleep_us(100);
                }
                ac->currentBuffer = 0;
                ac->currentBufferReadPos = 0;
            }
        }
    }
    else if(ac->got.format == AUDIO_F32LSB && ac->got.freq == 44100 && ac->got.channels == 2 )   // 44k stereo with 32bit
    {
        float* streamOut = (float*)streamOutPtr;
        int stretchCnt = 44100/11025;

        uint32_t writtenBytes = 0;
        while(writtenBytes < len / sizeof(float))
        {
            // pop next buffer
            if(!ac->currentBuffer)
            {            
                while(!queue_try_remove(&ac->outputPool, &ac->currentBuffer))
                {                    
                    // NOTE: this is a an ongoing bug but no idea how to fix this, first attempt was the mutex but seems to be accessed by another thread somehow?  
                    test_core_apu_render_audio();
                    ac->starveCnt++;
                }
                ac->currentBufferReadPos = 0;
            }

            // write buffer bytes        
            int16_t* streamIn = (int16_t*)ac->currentBuffer->buffer->bytes;
            for(int i=ac->currentBufferReadPos;i<ac->currentBuffer->buffer->size / sizeof(int16_t);i+=2)
            {
                float sample0 = (streamIn[ac->currentBufferReadPos + 0]) / 32767.5f;
                float sample1 = (streamIn[ac->currentBufferReadPos + 1]) / 32767.5f;

                for(int j=0;j<stretchCnt;j++)
                {
                    streamOut[writtenBytes++] = sample0;
                    streamOut[writtenBytes++] = sample1;
                }

                if(writtenBytes >= len)
                    break;        

                ac->currentBufferReadPos += 2;
            }

            // pop current
            if(ac->currentBufferReadPos >= ac->currentBuffer->buffer->size/ sizeof(int16_t))
            {
                while(!queue_try_add(&ac->freePool, &ac->currentBuffer))
                {                
                    tight_loop_contents();
                    picocom_sleep_us(100);
                }
                ac->currentBuffer = 0;
                ac->currentBufferReadPos = 0;
            }
        }
    }   
    else if(ac->got.format == AUDIO_F32LSB && ac->got.freq == 48000 && ac->got.channels == 2 )   // 44->48k stereo mode hack, ideally want 4:1 but will just stretch ( simulated targets only )
    {
        float* streamOut = (float*)streamOutPtr;
        int stretchCnt = 48000/11025;

        uint32_t writtenBytes = 0;
        while(writtenBytes < len / sizeof(float))
        {
            // pop next buffer
            if(!ac->currentBuffer)
            {            
                while(!queue_try_remove(&ac->outputPool, &ac->currentBuffer))
                {
                    test_core_apu_render_audio();
                    ac->starveCnt++;
                }
                ac->currentBufferReadPos = 0;
            }

            // write buffer bytes        
            int16_t* streamIn = (int16_t*)ac->currentBuffer->buffer->bytes;
            for(int i=ac->currentBufferReadPos;i<ac->currentBuffer->buffer->size / sizeof(int16_t);i+=2)
            {
                float sample0 = (streamIn[ac->currentBufferReadPos + 0]) / 32767.5f;
                float sample1 = (streamIn[ac->currentBufferReadPos + 1]) / 32767.5f;

                for(int j=0;j<stretchCnt;j++)
                {
                    streamOut[writtenBytes++] = sample0;
                    streamOut[writtenBytes++] = sample1;
                }

                if(writtenBytes >= len)
                    break;        

                ac->currentBufferReadPos += 2;
            }

            // pop current
            if(ac->currentBufferReadPos >= ac->currentBuffer->buffer->size/ sizeof(int16_t))
            {
                while(!queue_try_add(&ac->freePool, &ac->currentBuffer))
                {                
                    tight_loop_contents();
                    picocom_sleep_us(100);
                }
                ac->currentBuffer = 0;
                ac->currentBufferReadPos = 0;
            }
        }
    }      
    else
    {
        static bool shownWarn = false;
        if(!shownWarn)
        {
            shownWarn = true;
            printf("Audio format: %x, freq: %d, channels: %d not supported\n", ac->got.format, ac->got.freq, ac->got.channels);
        }
        memset(streamOutPtr,0,len);
    }
}


inline static bool pico_buffer_alloc_in_place(mem_buffer_t *buffer, size_t size) {
#ifdef PICO_BUFFER_USB_ALLOC_HACK
    extern uint8_t *usb_ram_alloc_ptr;
    if ((usb_ram_alloc_ptr + size) <= (uint8_t *)USBCTRL_DPRAM_BASE + USB_DPRAM_SIZE) {
        buffer->bytes = usb_ram_alloc_ptr;
        buffer->size = size;
#ifdef DEBUG_MALLOC
        printf("balloc %d %p->%p\n", size, buffer->bytes, ((uint8_t *)buffer->bytes) + size);
#endif
        usb_ram_alloc_ptr += size;
        return true;
    }
#endif    // inline for now
    buffer->bytes = (uint8_t *) calloc(1, size);
    if (buffer->bytes) {
        buffer->size = size;
        return true;
    }
    buffer->size = 0;
    return false;
}

inline static mem_buffer_t *pico_buffer_alloc(size_t size) {
    mem_buffer_t *b = (mem_buffer_t *) malloc(sizeof(mem_buffer_t));
    if (b) {
        if (!pico_buffer_alloc_in_place(b, size)) {
            free(b);
            b = NULL;
        }
    }
    return b;
}

struct audio_buffer_pool* audio_sdl2_connect(  audio_buffer_format_t *format, int buffer_count, int buffer_sample_count )
{
    // Wait
    while(!sdlDisplayStateValid)
    {
        picocom_sleep_us(10000);
    }

    if(!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO))
    {        
        return 0;
    }

    struct audio_buffer_pool* ac = (struct audio_buffer_pool*)picocom_malloc(sizeof(struct audio_buffer_pool));
    if(!ac)
        return 0;
    memset(ac, 0, sizeof(struct audio_buffer_pool));
    ac->audioEnabled = 0;

    // alloc buffers, fixed sample rate and channels ( 16bit * 2 )
    queue_init(&ac->freePool, sizeof(audio_buffer_t*), buffer_count * 2);
    queue_init(&ac->outputPool, sizeof(audio_buffer_t*), buffer_count * 2);

    audio_buffer_t *audio_buffers = buffer_count ? (audio_buffer_t *) calloc(buffer_count, sizeof(audio_buffer_t)) : 0;        
    for (int i = 0; i < buffer_count; i++) 
    {      
        audio_buffer_t *audio_buffer = audio_buffers + i;
        audio_buffer->buffer = pico_buffer_alloc( buffer_sample_count * format->sample_stride );
        audio_buffer->max_sample_count = buffer_sample_count;
        audio_buffer->sample_count = 0;
        audio_buffer->buffer_alloc_size = buffer_sample_count * format->sample_stride;
        audio_buffer->format = format;

        if(!queue_try_add(&ac->freePool, &audio_buffer))
        {
            picocom_panic(SDKErr_Fail, "Failed to queue audio buffer");
        }                                            
    }

    ac->audio_buffers = audio_buffers;

    SDL_memset(&(ac->want), 0, sizeof(ac->want));

    ac->want.freq =  AUDIO_FREQUENCY;
    ac->want.format = AUDIO_FORMAT;
    ac->want.channels = AUDIO_CHANNELS;
    ac->want.samples = AUDIO_SAMPLES;

    // Force resample to match wasm hackery for demo,    
    /*
    Eg. wasm spec:
        audio.obtained.channels: 2
        audio.obtained.format: 33056
        audio.obtained.freq: 44100
        audio.obtained.samples: 4096        
    */
    ac->want.freq =  44100;
    ac->want.format = AUDIO_F32LSB;     // float 32
    ac->want.channels = 2;
    ac->want.samples = 4096;

    ac->want.callback = audioCallback;
    ac->want.userdata = ac;    
    if((ac->device = SDL_OpenAudioDevice(NULL, 0, &(ac->want), &ac->got, SDL_AUDIO_ALLOW_ANY_CHANGE)) == 0)
    {
        return 0;
    }
    else
    {
        ac->audioEnabled = 1;
        SDL_PauseAudioDevice(ac->device, 0);

        printf("audio.obtained.channels: %d\n", ac->got.channels);
        printf("audio.obtained.format: %x\n", ac->got.format);
        printf("audio.obtained.freq: %d\n", ac->got.freq);
        printf("audio.obtained.samples: %d\n", ac->got.samples);                
    }

    return ac;
}


void audio_sdl2_disconnect(struct audio_buffer_pool* ac)
{
    SDL_CloseAudioDevice(ac->device);
    picocom_free(ac);
}


audio_buffer_t *take_audio_buffer(audio_buffer_pool_t *ac, bool block)
{
    audio_buffer_t* result = 0;
    if(queue_try_remove(&ac->freePool, &result)) 
    {        
        return result;
    }

    return 0;
}


void give_audio_buffer(audio_buffer_pool_t *ac, audio_buffer_t *buffer)
{  
    if(!queue_try_add(&ac->outputPool, &buffer))
    {
        picocom_panic(SDKErr_Fail, "Failed to queue audio buffer");
    }
}


void sdl_audio_disable_audio()
{
    g_AudioThreadEnabled = false;
}

#else   // Alsa port for linux, this was due to sdl2 having some insane lag on ubuntu22.

// Externs
extern bool sdlDisplayStateValid;


// asound
#include <pthread.h>
#include <alsa/asoundlib.h>
#define SAMPLE_CNT 256
static bool g_AudioThreadEnabled = true;
static bool g_LogAlsaErrors = false;

snd_pcm_t *pcm;

#define check(ret)                                                          \
    do {                                                                    \
        int res = (ret);                                                    \
        if (res < 0) {                                                      \
            fprintf(stderr, "%s:%d ERROR: %s (%d)\n",                       \
                __FILE__, __LINE__, snd_strerror(res), res);                \
            exit(1);                                                        \
        }                                                                   \
    } while (0)


float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void mini_alsa_init()
{    
    check(snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0));

    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);

    check(snd_pcm_hw_params_any(pcm, hw_params));
    check(snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED));
    check(snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_S16_LE));
    check(snd_pcm_hw_params_set_channels(pcm, hw_params, AUDIO_CHANNELS));
    check(snd_pcm_hw_params_set_rate(pcm, hw_params, AUDIO_FREQUENCY, 0));
    check(snd_pcm_hw_params_set_periods(pcm, hw_params, 4, 0));
    check(snd_pcm_hw_params_set_period_time(pcm, hw_params, 10000, 0));    
    check(snd_pcm_hw_params(pcm, hw_params));
}


void mini_alsa_write(uint8_t* bufferIn)
{
    static int init = false;
    if(!init)
    {
        init = 1;
        mini_alsa_init();
    }

    snd_pcm_sframes_t res = snd_pcm_writei(pcm, bufferIn, SAMPLE_CNT);
    if(res < 0)
    {
        if(g_LogAlsaErrors)
        {
            if(res == -EBADFD)
            {
                printf("EBADFD\n");
            }
            else if(res == -EPIPE)
            {
                printf("EPIPE(underrun)\n");
            }
            else if(res == -ESTRPIPE)
            {
                printf("ESTRPIPE\n");
            }        
            else
            {
                printf("Unkown alsa error: %d\n", (int)res);
            }
        }

        snd_pcm_recover(pcm, res, 0);
    }
}


void alsa_thread(void *arg)
{
    audio_buffer_pool_t *ac = (audio_buffer_pool_t*)arg;
    while(g_AudioThreadEnabled)
    {
        {
            // pop next buffer
            if(!ac->currentBuffer)
            {            
                queue_remove_blocking(&ac->outputPool, &ac->currentBuffer);
                ac->currentBufferReadPos = 0;
            }
    
            mini_alsa_write((uint8_t*)ac->currentBuffer->buffer->bytes);

            while(!queue_try_add(&ac->freePool, &ac->currentBuffer))
            {                
                tight_loop_contents();
                picocom_sleep_us(100);
            }
            ac->currentBuffer = 0;
            ac->currentBufferReadPos = 0;
        }            
    }
}


inline static bool pico_buffer_alloc_in_place(mem_buffer_t *buffer, size_t size) {
#ifdef PICO_BUFFER_USB_ALLOC_HACK
    extern uint8_t *usb_ram_alloc_ptr;
    if ((usb_ram_alloc_ptr + size) <= (uint8_t *)USBCTRL_DPRAM_BASE + USB_DPRAM_SIZE) {
        buffer->bytes = usb_ram_alloc_ptr;
        buffer->size = size;
#ifdef DEBUG_MALLOC
        printf("balloc %d %p->%p\n", size, buffer->bytes, ((uint8_t *)buffer->bytes) + size);
#endif
        usb_ram_alloc_ptr += size;
        return true;
    }
#endif    // inline for now
    buffer->bytes = (uint8_t *) calloc(1, size);
    if (buffer->bytes) {
        buffer->size = size;
        return true;
    }
    buffer->size = 0;
    return false;
}

inline static mem_buffer_t *pico_buffer_alloc(size_t size) {
    mem_buffer_t *b = (mem_buffer_t *) malloc(sizeof(mem_buffer_t));
    if (b) {
        if (!pico_buffer_alloc_in_place(b, size)) {
            free(b);
            b = NULL;
        }
    }
    return b;
}

struct audio_buffer_pool* audio_sdl2_connect(  audio_buffer_format_t *format, int buffer_count, int buffer_sample_count )
{
    // Wait
    while(!sdlDisplayStateValid)
    {
        picocom_sleep_us(10000);
    }

    struct audio_buffer_pool* ac = (struct audio_buffer_pool*)picocom_malloc(sizeof(struct audio_buffer_pool));
    if(!ac)
        return 0;
    memset(ac, 0, sizeof(struct audio_buffer_pool));
    ac->audioEnabled = 0;

    // alloc buffers, fixed sample rate and channels ( 16bit * 2 )
    queue_init(&ac->freePool, sizeof(audio_buffer_t*), buffer_count * 2);
    queue_init(&ac->outputPool, sizeof(audio_buffer_t*), buffer_count * 2);

    audio_buffer_t *audio_buffers = buffer_count ? (audio_buffer_t *) calloc(buffer_count, sizeof(audio_buffer_t)) : 0;        
    for (int i = 0; i < buffer_count; i++) 
    {      
        audio_buffer_t *audio_buffer = audio_buffers + i;
        audio_buffer->buffer = pico_buffer_alloc( buffer_sample_count * format->sample_stride );
        audio_buffer->max_sample_count = buffer_sample_count;
        audio_buffer->sample_count = 0;
        audio_buffer->buffer_alloc_size = buffer_sample_count * format->sample_stride;
        audio_buffer->format = format;

        if(!queue_try_add(&ac->freePool, &audio_buffer))
        {
            picocom_panic(SDKErr_Fail, "Failed to queue audio buffer");
        }                                            
    }

    ac->audio_buffers = audio_buffers;

    // Alsa writer
    static pthread_t sdl_audio_thread;
    pthread_create(&sdl_audio_thread, NULL, alsa_thread, (void*)ac);

    return ac;
}


void audio_sdl2_disconnect(struct audio_buffer_pool* ac)
{
    SDL_CloseAudioDevice(ac->device);
    picocom_free(ac);
}


audio_buffer_t *take_audio_buffer(audio_buffer_pool_t *ac, bool block)
{
    audio_buffer_t* result = 0;
    if(queue_try_remove(&ac->freePool, &result)) 
    {        
        return result;
    }

    return 0;
}


void give_audio_buffer(audio_buffer_pool_t *ac, audio_buffer_t *buffer)
{  
    if(!queue_try_add(&ac->outputPool, &buffer))
    {
        picocom_panic(SDKErr_Fail, "Failed to queue audio buffer");
    }
}


void sdl_audio_disable_audio()
{
    g_AudioThreadEnabled = false;
}


#endif