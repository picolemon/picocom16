//#define APU_TRACE_CMD_BUFFERS

#include "picocom/devkit.h"
#include "memory.h"
#include "audio_engine.h"
#include "picocom/audio/audio.h"
#include "platform/pico/hw/picocom_hw.h"
#include <stdlib.h>
#include <assert.h>

// simulated flash 
#ifdef PICOCOM_SDL
    int save_and_disable_interrupts();
    int restore_interrupts (int flags);
    void flash_range_erase (uint32_t flash_offs, size_t count);
    void flash_range_program (uint32_t flash_offs, const uint8_t *data, size_t count);
    void flash_simulator_set_xip_bases (uint8_t* xipBase, uint8_t* xipEnd);
#endif


//
//
static void audio_verify_channel_state_ex( AudioChannel_t* c )
{
}

void audio_engine_verify_ex(AudioEngineState_t* engine)
{
    if(!engine->m_Inited)
        return;

    for(int cid=0;cid<MaxChannels;cid++)
    {
        AudioChannel_t* c = &engine->m_Channels[cid];
        audio_verify_channel_state_ex(c);
    }
}


//
//
#ifndef PICOCOM_NATIVE_SIM
size_t sd_read_func  (void *dstPtr, size_t size, size_t nmemb, void *handle)
{
    BaseAudioStreamState_t* state = (BaseAudioStreamState_t*)handle;
    FIL* fp = (FIL*)state->file_fp;
    if(!fp)
        return 0;
    uint8_t* dst = (uint8_t*)dstPtr;

    UINT read;
    FRESULT res = f_read (
        fp,
        dst,	
        size*nmemb,
        &read
    );

    if(res != FR_OK)
        return -1;

    return read;
}

int sd_seek_func  (void *handle, ogg_int64_t offset, int whence)
{
    BaseAudioStreamState_t* state = (BaseAudioStreamState_t*)handle;
    FIL* fp = (FIL*)state->file_fp;
    if(!fp)
        return 0;
    
    if(whence == SEEK_SET)
    {
        f_lseek( fp, offset );
    }
    else if(whence == SEEK_CUR)
    {
        f_lseek( fp,  f_tell(fp) + offset );       
    }
    else if(whence == SEEK_END)
    {
       f_lseek( fp, f_size(fp) );
    }    
    else
    {
        return 1;
    }
    
    return 0;
}

int sd_close_func (void *handle)
{
    BaseAudioStreamState_t* state = (BaseAudioStreamState_t*)handle;
    FIL* fp = (FIL*)state->file_fp;
    if(!fp)
        return 0;
    f_close(fp);
    return 0;
}

long sd_tell_func  (void *handle)
{
    BaseAudioStreamState_t* state = (BaseAudioStreamState_t*)handle;
    FIL* fp = (FIL*)state->file_fp;
    if(!fp)
        return 0;
    return f_tell(fp);    
}

/** Reads from sdcard storage
 */
static ov_callbacks OV_CALLBACKS_SD = {
  (size_t (*)(void *, size_t, size_t, void *))  sd_read_func,
  (int (*)(void *, ogg_int64_t, int))           sd_seek_func,
  (int (*)(void *))                             sd_close_func,
  (long (*)(void *))                            sd_tell_func
};
#endif


//
//
size_t ram_read_func  (void *ptr, size_t size, size_t nmemb, void *handle)
{
    BaseAudioStreamState_t* state = (BaseAudioStreamState_t*)handle;
    RamFile_t* fp = (RamFile_t*)&state->ramFp;
    if(!fp || !fp->sourceData)
        return 0;        
    uint8_t* dst = (uint8_t*)ptr;

    size_t res = 0;
    for(int i=0;i<size*nmemb;i++)
    {
        if( fp->streamOffset > fp->sourceSz || !fp->sourceData )
            break;
        dst[i] = fp->sourceData[ fp->streamOffset++ ];
        res++;
    }

    return res;
}

int ram_seek_func  (void *handle, ogg_int64_t offset, int whence)
{
    BaseAudioStreamState_t* state = (BaseAudioStreamState_t*)handle;
    RamFile_t* fp = (RamFile_t*)&state->ramFp;
    if(!fp)
        return 0;   

    if(whence == SEEK_SET)
    {
        fp->streamOffset = offset;
    }
    else if(whence == SEEK_CUR)
    {
        fp->streamOffset += offset;
    }
    else if(whence == SEEK_END)
    {
        fp->streamOffset = fp->sourceSz;
    }    
    else
    {
        return 1;
    }
    
    return 0;
}

int ram_close_func (void *handle)
{
    BaseAudioStreamState_t* state = (BaseAudioStreamState_t*)handle;
    RamFile_t* fp = (RamFile_t*)&state->ramFp;
    if(!fp)
        return 0;   
    return fp->streamOffset = 0;
}

long ram_tell_func  (void *handle)
{
    BaseAudioStreamState_t* state = (BaseAudioStreamState_t*)handle;
    RamFile_t* fp = (RamFile_t*)&state->ramFp;
    if(!fp)
        return 0;   
    return fp->streamOffset;
}


/** Reads from ram/flash storage
 */
static ov_callbacks OV_CALLBACKS_RAM = {
  (size_t (*)(void *, size_t, size_t, void *))  ram_read_func,
  (int (*)(void *, ogg_int64_t, int))           ram_seek_func,
  (int (*)(void *))                             ram_close_func,
  (long (*)(void *))                            ram_tell_func
};


//
//
AudioEngineState_t* audio_engine_init()
{
    AudioEngineState_t* state = (AudioEngineState_t*)malloc(sizeof(AudioEngineState_t));
    memset(state, 0, sizeof(AudioEngineState_t));
    state->m_SampleFreq = 11025;            
    state->m_EnableOutput = true;

    struct audio_buffer_pool *producer_pool;

    struct audio_format audio_format = {
            .sample_freq = state->m_SampleFreq,
            .format = AUDIO_BUFFER_FORMAT_PCM_S16,            
            .channel_count = 2,
    }; 

    struct audio_buffer_format producer_format = {
            .format = &audio_format,
            .sample_stride = 4
    };    

#ifdef PICOCOM_SDL
    producer_pool = audio_sdl2_connect( &producer_format, 2, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT );
#else   

    // NOTE: sizing aligned to ogg sampling
    producer_pool = audio_new_producer_pool(&producer_format, 2, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT );

    struct audio_i2s_config config = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = 6,
        .pio_sm = APU_I2S_SM,
    };

    const struct audio_format *output_format;
    output_format = audio_i2s_setup(&audio_format, &config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    bool ok = audio_i2s_connect_extra(producer_pool, false, 0, 0, NULL);
    assert(ok);
    audio_i2s_set_enabled(true);    
#endif
    state->m_BufferPool = producer_pool;
    
    for(int i=0;i<MaxAudioClips;i++)
    {
        state->m_Clips[i].clipId = -1;      
    }
    
    for(int i=0;i<MaxChannels;i++)
    {        
        struct AudioChannel_t* c = &state->m_Channels[i];
        c->sampleStep = 1.0;    
        c->volLeft = 1.0;
        c->volRight = 1.0;
    }
    
    if( flash_store_init_xip( &state->flashStore, 0, AUDIO_FLASH_STORAGE_SIZE ) != SDKErr_OK )
        picocom_panic( SDKErr_Fail, "flash_store_init_ram failed");

    state->m_Inited = true;
    
    return state;
}


void audio_engine_deinit(AudioEngineState_t* engine)
{
#ifdef PICOCOM_SDL
    if( engine->m_BufferPool )
        audio_sdl2_disconnect( engine->m_BufferPool );
     engine->m_BufferPool = 0;
#else
    picocom_panic(SDKErr_Fail, "de-init not enabled on hw, device reboot required!");
#endif

}


bool audio_engine_playRaw( AudioEngineState_t* engine, uint32_t channelId, const AudioChannel_t* channelState )
{
    if(channelId >= MaxChannels)
        return false;

    AudioChannel_t* c = &engine->m_Channels[channelId];        
    audio_engine_stop(engine, channelId);

    if(!channelState || !channelState->clip)
        return false;

    // handle data source
#ifndef PICOCOM_NATIVE_SIM    
    FIL* file_fp = 0;                             // File pointer for sourceType
#endif    
    ov_callbacks fpHandler;
    switch (channelState->clip->sourceType)
    {
    case EAudioDataSource_Ram:
    {
        fpHandler = OV_CALLBACKS_RAM;

        // Alloc fp
        RamFile_t* ramFp = (RamFile_t*)&c->streamState.ramFp;        
        memset(ramFp, 0, sizeof(RamFile_t));
        ramFp->sourceData = (uint8_t*)channelState->clip->ramSourceData;
        ramFp->sourceSz = channelState->clip->sourceSz;
        ramFp->streamOffset = 0;

        break;
    }
#ifndef PICOCOM_NATIVE_SIM    
    case EAudioDataSource_File:
    {
        fpHandler = OV_CALLBACKS_SD;

        // Alloc fp
        FIL* sdFp = (FIL*)malloc(sizeof(FIL));
        if(!sdFp)
            return false;
        
        // fopen file handle
        FRESULT fr = f_open(sdFp, (const TCHAR*)channelState->clip->filename, FA_READ);
        if (fr != FR_OK) 
        {
            free(sdFp);
            return 0; // no img
        }

        file_fp = sdFp;
        break;        
    }
#endif    
    default:
        return false;        
    }
    
    // handle format
    switch(channelState->clip->dataFormat)
    {
        case EAudioDataFormat_Ogg:
        {           
            // alloc state
            struct BaseAudioStreamState_t* state = &c->streamState;
            state->sourceClip = channelState->clip;
#ifndef PICOCOM_NATIVE_SIM            
            state->file_fp = file_fp;
#endif            
            state->fpHandler = fpHandler;  

            state->vorbis = (struct OggVorbis_File*)malloc(sizeof(OggVorbis_File));
            memset(state->vorbis, 0, sizeof(OggVorbis_File));
            if(!state->vorbis)
                return false;

            ov_clear(state->vorbis);
            if(ov_open_callbacks((void*)state, state->vorbis, NULL, 0, fpHandler) != 0)
            {                
                return false;
            }

            // Get info
            vorbis_info* info = ov_info(state->vorbis, -1);            
            if(info->rate != engine->m_SampleFreq)
            {
                printf("Invalid ogg file %ld Hz, %d channels, %ld kbit/s.\n", info->rate, info->channels, info->bitrate_nominal / 1024);
                return false;
            }

            // capture file info            
            c->channels = info->channels;
                 
            break;
        }
        case EAudioDataFormat_RawPCM:
        {
            // alloc state
            struct BaseAudioStreamState_t* state = &c->streamState;
            state->sourceClip = channelState->clip;     
#ifndef PICOCOM_NATIVE_SIM                   
            state->file_fp = file_fp;
#endif            
            state->fpHandler = fpHandler;         

            // use source info from clip for raw format
            c->channels = channelState->clip->sourceChannels;
            break;
        }        
        default:
            return false;
    }

    // copy into channel
    c->clip = channelState->clip;    
    c->sampleOffset = channelState->sampleOffset;
    c->sampleStep = channelState->sampleStep;
    c->volLeft = channelState->volLeft;
    c->volRight = channelState->volRight;
    c->loop = channelState->loop;
    c->playingState = 1;
    
    return true;
}   


bool audio_engine_isPlaying( AudioEngineState_t* engine, uint32_t channelId )
{
    if(channelId >= MaxChannels)
        return false;

    struct AudioChannel_t* c = &engine->m_Channels[channelId];
    return c->playingState;  
}


AudioClip_t* audio_engine_getClipById( AudioEngineState_t* engine, uint8_t clipId )
{
    if(clipId > MaxAudioClips)
        return 0;
      
    return &engine->m_Clips[clipId];
}


void audio_engine_stop( AudioEngineState_t* engine, uint32_t channelId )
{
    // free
    if(channelId >= MaxChannels)
        return;

    AudioChannel_t* c = &engine->m_Channels[channelId];    
        
    c->playingState = 0;

    if(!c->clip)
        return;
    
    // free state
    if(c->streamState.vorbis)
    {
        ov_clear(c->streamState.vorbis);
        free(c->streamState.vorbis);
        c->streamState.vorbis = 0;
    }

    if(c->streamState.nextData)
    {
        audio_engine_putPooledDecoderBlock(engine, c, c->streamState.nextData, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
        c->streamState.nextData = 0;        
    }

    if(c->streamState.decodedData)
    {
        audio_engine_putPooledDecoderBlock(engine, c, c->streamState.decodedData, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
        c->streamState.decodedData = 0;        
    }
#ifndef PICOCOM_NATIVE_SIM
    // close file handle
    if(c->streamState.file_fp)
    {      
        f_close( (FIL*)c->streamState.file_fp);  
        free(c->streamState.file_fp);          
        c->streamState.file_fp = 0;
    }
#endif    
    memset(&c->streamState, 0, sizeof(c->streamState));       
}   



void audio_engine_updateAudio( AudioEngineState_t* engine )
{
    if(!engine->m_Inited)
        return;

    // render next
    struct audio_buffer *outputAudioBuffer = take_audio_buffer(engine->m_BufferPool, false);
    if(!outputAudioBuffer)
        return;

    {
        int16_t *outputSamples = (int16_t *)outputAudioBuffer->buffer->bytes;
        for(int i=0;i<outputAudioBuffer->max_sample_count;i++)
        {
            *outputSamples++ = 0;
            *outputSamples++ = 0;
        }
    }
    
    int processedSampleCnt = 0;
    
    for(int cid=0;cid<MaxChannels;cid++)
    {
        AudioChannel_t* c = &engine->m_Channels[cid];          

        if(!c->playingState || !c->clip || !c->streamState.decodedData || c->streamState.eof )
        {            
            continue;
        }
        
        int16_t *outputSamples = (int16_t *)outputAudioBuffer->buffer->bytes;
        
        int sampleOffsetEnd = c->streamState.decodedSz;
        if(sampleOffsetEnd <= 0)
        {        
            if(!c->isStreaming){
                audio_engine_stop(engine, cid);
            }
            continue;
        }
        
        for(int i=0;i<outputAudioBuffer->max_sample_count;i++)
        {
            if( c->channels == 1 )
            {
                int16_t* srcSampleData16 = c->streamState.decodedData;

                // ensure
                if(c->sampleOffset > sampleOffsetEnd)
                    c->sampleOffset = sampleOffsetEnd - 1;

                int16_t left = srcSampleData16[ (int)(c->sampleOffset) ];

                c->sampleOffset += c->sampleStep;

                if( (int)(c->sampleOffset) >= sampleOffsetEnd)
                {
                    // pop next buffer
                    if(c->streamState.nextData)
                    {
                        audio_engine_putPooledDecoderBlock(engine, c, c->streamState.decodedData, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
                        c->streamState.decodedData = 0;

                        // swap
                        c->streamState.decodedData = c->streamState.nextData;
                        c->streamState.decodedSz =  c->streamState.nextSz;
                        c->sampleOffset = 0;
                        
                        c->streamState.nextData = 0;
                        c->streamState.nextSz = 0;                        
                    }
                    else
                    {                        
                        // stall
                        break; // underflow, will glitch otherwise will just stop for now
                    }
                }
                                    
                *outputSamples++ += (left * c->volLeft);
                *outputSamples++ += (left * c->volRight);
            }
            else if( c->channels == 2 )
            {
                int16_t* srcSampleData16 = c->streamState.decodedData;
                if(!srcSampleData16)
                    break;

                // ensure
                if(c->sampleOffset > sampleOffsetEnd)
                    c->sampleOffset = sampleOffsetEnd - 1;

                int16_t left = srcSampleData16[ ((int)(c->sampleOffset) * 2) ];
                int16_t right = srcSampleData16[ ((int)(c->sampleOffset) * 2) + 1 ];

                c->sampleOffset += c->sampleStep;

                if( (int)(c->sampleOffset) >= sampleOffsetEnd)
                {
                    // pop next buffer
                    if(c->streamState.nextData)
                    {
                        audio_engine_putPooledDecoderBlock(engine, c, c->streamState.decodedData, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
                        c->streamState.decodedData = 0;

                        // swap
                        c->streamState.decodedData = c->streamState.nextData;
                        c->streamState.decodedSz =  c->streamState.nextSz;
                        c->sampleOffset = 0;
                        
                        c->streamState.nextData = 0;
                        c->streamState.nextSz = 0;                        
                    }
                    else
                    {                                                   
                        break; // underflow, will glitch otherwise will just stop for now
                    }
                }
                                    
                *outputSamples++ += (left * c->volLeft);
                *outputSamples++ += (right * c->volRight);
            }
        }
    }

    outputAudioBuffer->sample_count = outputAudioBuffer->max_sample_count; //processedSampleCnt;
    give_audio_buffer(engine->m_BufferPool, outputAudioBuffer);



}


void audio_engine_updateDataStreams(AudioEngineState_t* engine)
{
    if(!engine->m_Inited)
        return;

    for(int cid=0;cid<MaxChannels;cid++)
    {
        AudioChannel_t* c = &engine->m_Channels[cid];

        if(!c->playingState || !c->clip || c->streamState.eof)
        {
            
            continue;
        }

        if (c->clip->dataFormat == EAudioDataFormat_RawPCM)        
        {
            // fill next buffer ( if ready )
            if(!c->streamState.nextData || !c->streamState.decodedSz)
            {                            
                // get pooled block
                if(!c->streamState.nextData)
                    c->streamState.nextData = audio_engine_getPooledDecoderBlock(engine, c, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
                if(!c->streamState.nextData)
                {                   
                    // NOTE: dont stop if stream
                    if(!c->isStreaming)                    
                        audio_engine_stop(engine, cid);                    
                    continue;
                }
               
                c->streamState.nextSz = AUDIO_SAMPLE_BLOCK_SAMPLE_CNT;

                // read next block in stream using fp handler    
                int readSz = c->streamState.fpHandler.read_func( 
                    c->streamState.nextData,
                    AUDIO_SAMPLE_BLOCK_SAMPLE_CNT*sizeof(int16_t)*c->channels, 1, 
                    &c->streamState
                   );

                if(readSz <= 0)
                {
                    if(c->loop)
                    {
                        c->playId++;
                        c->streamState.fpHandler.seek_func( &c->streamState, 0, SEEK_SET);

                        readSz = c->streamState.fpHandler.read_func( 
                                c->streamState.nextData,
                                AUDIO_SAMPLE_BLOCK_SAMPLE_CNT*sizeof(int16_t)*c->channels, 1, 
                                &c->streamState
                            );

                        if(readSz <= 0)
                        {
                            audio_engine_stop(engine, cid);
                            continue;
                        }
                    }
                    else
                    {                  
                        if(c->isStreaming)
                        {
                            int bufferCnt = audio_get_channel_clip_seq_queue_size(c);
                            
                            if(c->readingClipSeq != c->writingClipSeq && bufferCnt >= c->minStreamBufferSeqCnt )
                            {
                                if(c->streamState.nextData)
                                {
                                    audio_engine_putPooledDecoderBlock(engine, c, c->streamState.nextData, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
                                    c->streamState.nextData = 0;                                    
                                }

                                if(c->streamState.decodedData)
                                {                                   
                                    audio_engine_putPooledDecoderBlock(engine, c, c->streamState.decodedData, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
                                    c->streamState.decodedData = 0;                                    
                                }
                                                            
                                uint32_t nextClipId = c->clipSeq[c->readingClipSeq];
                                c->readingClipSeq = (c->readingClipSeq + 1) % NUM_ELEMS(c->clipSeq);
                                
                                // Trigger play if clip is valid
                                AudioClip_t* newClip = audio_engine_getClipById( engine, nextClipId );

                                if( newClip && newClip->dataFormat != EAudioDataFormat_None )
                                {                                                           
                                    struct BaseAudioStreamState_t* state = &c->streamState; 
                                    memset(&c->streamState.ramFp,0, sizeof(c->streamState.ramFp));
                                    c->clip = newClip;  
                                    c->sampleOffset = 0;
                                    c->playingState = 1;
                                    state->ramFp.streamOffset = 0;
                                    state->ramFp.sourceData = newClip->ramSourceData;
                                    state->ramFp.sourceSz = newClip->sourceSz;
                                    state->sourceClip = newClip;

                                    // fill next buffer ( if ready )
                                    if(!c->streamState.nextData)
                                    {
                                        // get pooled block
                                        c->streamState.nextData = audio_engine_getPooledDecoderBlock(engine, c, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
                                        if(!c->streamState.nextData)
                                        {                                                                              
                                            continue;
                                        }
                                        c->streamState.nextSz = AUDIO_SAMPLE_BLOCK_SAMPLE_CNT;

                                        // read next block in stream using fp handler    
                                        int readSz = c->streamState.fpHandler.read_func( 
                                            c->streamState.nextData,
                                            AUDIO_SAMPLE_BLOCK_SAMPLE_CNT*sizeof(int16_t)*c->channels, 1, 
                                            &c->streamState
                                        );

                                        if(readSz <= 0)
                                        {
                                            if(c->loop)
                                            {
                                                c->streamState.fpHandler.seek_func( &c->streamState, 0, SEEK_SET);

                                                readSz = c->streamState.fpHandler.read_func( 
                                                        c->streamState.nextData,
                                                        AUDIO_SAMPLE_BLOCK_SAMPLE_CNT*sizeof(int16_t)*c->channels, 1, 
                                                        &c->streamState
                                                    );

                                                if(readSz <= 0)
                                                {
                                                    audio_engine_stop(engine, cid);                                                   
                                                    continue;
                                                }
                                            }
                                            else
                                            {
                                                // seq
                                                if(c->readingClipSeq != c->writingClipSeq)
                                                {
                                                                            
                                                }

                                                // EOF
                                                c->streamState.eof = true;                                               
                                                continue;
                                            }
                                        }

                                        c->streamState.nextSz = (readSz/sizeof(int16_t)) / c->channels; // get size in samples
                                        if(readSz == -1)
                                            picocom_panic(0, "!readSz");
                                            
                                        // Load first
                                        if(!c->streamState.decodedData)
                                        {
                                            // swap
                                            c->streamState.decodedData = c->streamState.nextData;
                                            c->streamState.decodedSz =  c->streamState.nextSz;
                                            c->sampleOffset = 0;

                                            c->streamState.nextData = 0;
                                            c->streamState.nextSz = 0;                                            
                                        }
                                    }                        
                                }   
                            }
                            else
                            {
                                c->streamUnderflowCnt++;
                           //     printf("u %d, b %d\n",  c->streamUnderflowCnt, bufferCnt);
                            }
                        }
                        else
                        {
                            // EOF
                            c->streamState.eof = true;                           
                            continue;
                        }
                    }               
                }

                c->streamState.nextSz = (readSz/sizeof(int16_t)) / c->channels; // get size in samples
                if(readSz == -1)
                    picocom_panic(0, "!readSz");
                    
                    
                // Load first
                if(!c->streamState.decodedData)
                {
                    // swap
                    c->streamState.decodedData = c->streamState.nextData;
                    c->streamState.decodedSz =  c->streamState.nextSz;
                    c->sampleOffset = 0;

                    c->streamState.nextData = 0;
                    c->streamState.nextSz = 0;                    
                }
            }
        }
        else if (c->clip->dataFormat == EAudioDataFormat_Ogg)        
        {
            // fill next buffer ( if ready )
            if(!c->streamState.nextData)
            {        
                      
                // get pooled block
                c->streamState.nextData = audio_engine_getPooledDecoderBlock(engine, c, AUDIO_SAMPLE_BLOCK_SAMPLE_CNT, c->channels);
                if(!c->streamState.nextData)
                {
                    audio_engine_stop(engine, cid);                   
                    continue;
                }

                if( !c->streamState.vorbis || !c->streamState.nextData )
                {
                    audio_engine_stop(engine, cid);                   
                    continue;
                }

                // read ogg samples
                // CRASH: vorbis == 0 && nextData == 0, this isn't threaded so how does this pass above ?
                int word = 2;
                int bigendian = 0; 
                long readSamplesBytes = ov_read(
                    c->streamState.vorbis, 
                    (char*)c->streamState.nextData, 
                    AUDIO_SAMPLE_BLOCK_SAMPLE_CNT*sizeof(int16_t)*c->channels, 
                    bigendian, word, 1, 0);

                long readSamples = readSamplesBytes / c->channels / sizeof(int16_t);

                if(!readSamples)
                {
                    if(c->loop)
                    {
                        // re-seek
                        ov_time_seek( c->streamState.vorbis, 0);

                        readSamplesBytes = ov_read(
                            c->streamState.vorbis, 
                            (char*)c->streamState.nextData, 
                            AUDIO_SAMPLE_BLOCK_SAMPLE_CNT*sizeof(int16_t)*c->channels, 
                            bigendian, word, 1, 0);

                        readSamples = readSamplesBytes / c->channels / sizeof(int16_t);                        

                        if(!readSamples)
                        {
                            // Failed, mark as EOF
                            c->streamState.eof = true;
                            continue;
                        }
                    }
                    else
                    {
                        // EOF
                        c->streamState.eof = true;                
                        continue;
                    }
                }

                c->streamState.nextSz = readSamples; // get size in samples

                // Load first
                if(!c->streamState.decodedData)
                {
                    // swap
                    c->streamState.decodedData = c->streamState.nextData;
                    c->streamState.decodedSz =  c->streamState.nextSz;
                    c->sampleOffset = 0;

                    c->streamState.nextData = 0;
                    c->streamState.nextSz = 0;
                }
            }
        }
    }
}

int16_t* audio_engine_getPooledDecoderBlock(AudioEngineState_t* engine, AudioChannel_t* channel, int samples, int channels)
{   
    int reqSz = samples*sizeof(int16_t)*channels;
    if(reqSz > AUDIO_SAMPLE_BLOCK_SAMPLE_MAX_SZ)
        picocom_panic(SDKErr_Fail, "getPooledDecoderBlock overflow");

    for(int i=0;i<NUM_ELEMS(channel->buffers);i++)
    {
        if(channel->buffers[i].isActive)
            continue;
        channel->buffers[i].isActive = true;
        return (int16_t* )channel->buffers[i].buffer;
    }
    return 0;    
}


void audio_engine_putPooledDecoderBlock(AudioEngineState_t* engine, AudioChannel_t* channel, int16_t* data, int samples, int channels)
{   
    bool found = false;
    for(int i=0;i<NUM_ELEMS(channel->buffers);i++)
    {
        if(channel->buffers[i].isActive && channel->buffers[i].buffer == (uint8_t*)data)
        {
            channel->buffers[i].isActive = false;
            found = true;
            break;
        }
    }

    if(!found)
        picocom_panic(SDKErr_Fail, "!buffer found");
}


int audio_engine_stop_all_channels_for_clip_id(AudioEngineState_t* engine, uint16_t clipId)
{   
    AudioClip_t* clip = audio_engine_getClipById(engine, clipId );
    if(!clip)
        return 0;

    int result = 0;
    for(int cid=0;cid<MaxChannels;cid++)
    {
        AudioChannel_t* c = &engine->m_Channels[cid];
    
        if(c->clip == clip)
        {
            audio_engine_stop( engine, cid );
            result++;
        }
    }

    return result;
}


int audio_engine_free_clip(AudioEngineState_t* engine, uint16_t clipId)
{   
    // Ensure stopped
    audio_engine_stop_all_channels_for_clip_id(engine, clipId);

    // Get clip
    AudioClip_t* clip = audio_engine_getClipById(engine, clipId );
    if(!clip)
        return 0;

    switch(clip->sourceType)
    {
        case EAudioDataSource_Ram:
        {
            if(!clip->isFlash)
                free(clip->ramSourceData);
            memset(clip->filename, 0, sizeof(clip->filename));            
            clip->ramSourceData = 0;
        }
        case EAudioDataSource_File:
        {
            // free filename
            if(clip->ramSourceData)
                free(clip->ramSourceData);
            clip->ramSourceData = 0;
        }
    }   

    return 0;
}


int audio_engine_reset(AudioEngineState_t* engine)
{   
    // stop all channels
    int result = 0;
    for(int cid=0;cid<MaxChannels;cid++)
    {
        AudioChannel_t* c = &engine->m_Channels[cid];
        audio_engine_stop( engine, cid );
    }

    // free all clips
    for(int cid=0;cid<MaxAudioClips;cid++)    
    {
        audio_engine_free_clip(engine, cid);
    }

    return SDKErr_OK;
}


int audio_engine_create_clip_mem(AudioEngineState_t* engine, uint16_t clipId, enum EAudioDataFormat format, uint8_t sourceChannels, uint32_t memOffset, uint32_t size)
{   
    // Get clip
    AudioClip_t* clip = audio_engine_getClipById(engine, clipId );
    if(!clip)
        return SDKErr_Fail;

    if(audio_engine_free_clip(engine, clipId) != SDKErr_OK)
        return SDKErr_Fail;

    // Ensure free
    assert(!clip->ramSourceData);    
    // try alloc ram
    clip->ramSourceData = picocom_malloc(size);
    if(!clip->ramSourceData)
        return SDKErr_Fail;
    memset(clip->ramSourceData, 0, size);

    clip->clipId = clipId;
    clip->dataFormat = format;    
    clip->sourceType = EAudioDataSource_Ram;        
    clip->sourceChannels = sourceChannels;
    clip->sourceSz = size;
    clip->isFlash = false;

    // begin ram write
    return SDKErr_OK;    
}


int audio_engine_create_clip_flash(AudioEngineState_t* engine, uint16_t clipId, enum EAudioDataFormat format, uint8_t sourceChannels, uint32_t memOffset, uint32_t size)
{
    // Get clip
    AudioClip_t* clip = audio_engine_getClipById(engine, clipId );
    if(!clip)
        return SDKErr_Fail;

    if(audio_engine_free_clip(engine, clipId) != SDKErr_OK)
        return SDKErr_Fail;

    // Ensure free
    assert(!clip->ramSourceData);    

    clip->clipId = clipId;
    clip->dataFormat = format;    
    clip->sourceType = EAudioDataSource_Ram;        
    clip->sourceChannels = sourceChannels;
    clip->sourceSz = size;
    clip->isFlash = true;
    clip->ramSourceData = flash_store_get_ptr( &engine->flashStore, memOffset );

    // begin flash write
    return flash_store_begin( &engine->flashStore, memOffset );
}


int audio_engine_create_clip_embeded(AudioEngineState_t* engine, uint16_t clipId, enum EAudioDataFormat format, uint8_t sourceChannels, uint8_t* buffer, uint32_t size)
{
    // Get clip
    AudioClip_t* clip = audio_engine_getClipById(engine, clipId );
    if(!clip)
        return SDKErr_Fail;

    if(audio_engine_free_clip(engine, clipId) != SDKErr_OK)
        return SDKErr_Fail;

    // Ensure free
    assert(!clip->ramSourceData);    

    clip->clipId = clipId;
    clip->dataFormat = format;    
    clip->sourceType = EAudioDataSource_Ram;        
    clip->sourceChannels = sourceChannels;
    clip->sourceSz = size;
    clip->isFlash = true;
    clip->ramSourceData = buffer;

    return SDKErr_OK;
}


int audio_engine_write_clip_block( AudioEngineState_t* engine, uint8_t clipId, uint32_t offset, const uint8_t* data, uint32_t dataSize, uint8_t isLast)
{
    // Get clip
    AudioClip_t* clip = audio_engine_getClipById(engine, clipId );
    if(clip->isFlash)
    {
        int res = flash_store_next_write_block( &engine->flashStore, offset, data, dataSize);   
        if(res != SDKErr_OK)
            return res;

        if(isLast)
        {
            res = flash_store_end( &engine->flashStore );
        }

        return res;
    }
    else
    {
        if(!clip || !clip->ramSourceData || !clip->sourceSz)
            return SDKErr_Fail;

        if( offset + dataSize > clip->sourceSz)
            return SDKErr_Fail;

        memcpy(clip->ramSourceData + offset, data, dataSize);

        return SDKErr_OK;
    }
}


int audio_engine_queue_seq( AudioEngineState_t* engine, int32_t channelId, uint16_t clipId )
{
    // free
    if(channelId >= MaxChannels)
        return 0;

    AudioChannel_t* c = &engine->m_Channels[channelId];

    if ((c->writingClipSeq + 1) % NUM_ELEMS(c->clipSeq) == c->readingClipSeq)
    {        
        return 0; // full
    }

    c->clipSeq[ c->writingClipSeq ] = clipId;
    c->writingClipSeq = (c->writingClipSeq+  1) % NUM_ELEMS(c->clipSeq);
    c->isStreaming = true;
    c->minStreamBufferSeqCnt = 0;
    
    return 1;
}


int audio_get_channel_clip_seq_queue_size( AudioChannel_t* channel )
{
    int cnt = 0;
    int readingClipSeq = channel->readingClipSeq;
    while(readingClipSeq != channel->writingClipSeq)
    {
        uint32_t nextClipId = channel->clipSeq[readingClipSeq];
        readingClipSeq = (readingClipSeq + 1) % NUM_ELEMS(channel->clipSeq);    
        cnt++;
    }
    return cnt;
}