#include "picocom/devkit.h"
#include "audio.h"
#include "picocom/storage/storage.h"
#include "lib/platform/pico/hw/picocom_hw.h"
#include "lib/components/apu_core/apu_client.h"
#include "lib/platform/pico/apu/hw_apu_types.h"
#include "lib/components/flash_store/flash_store.h"
#include "thirdparty/miniz/miniz.h"
#include <stdio.h>

// Globals
struct AudioState_t* g_AudioState = 0;
void test_service_audio_main(void* userData); // service api ( non-callouts to bus )
void test_core_apu_update(); // tick audio
void test_core_apu_full_update();
void storage_reset();


//
//
struct AudioOptions_t audio_default_options()
{
    struct AudioOptions_t options = (struct AudioOptions_t){
        .enabled = true,
    };
    return options;
}


int audio_init()
{
    struct AudioOptions_t options = audio_default_options();
    return audio_init_with_options(&options);
}


int audio_init_with_options(struct AudioOptions_t* options)
{
    if(g_AudioState)
        return SDKErr_Fail;

    g_AudioState = (struct AudioState_t*)picocom_malloc(sizeof(struct AudioState_t));
    memset(g_AudioState, 0, sizeof(AudioState_t));

    BusTx_t* apuLink_tx = &g_AudioState->apuLink_tx;
    BusRx_t* apuLink_rx = &g_AudioState->apuLink_rx;
    
    // Init bus
    bus_tx_configure(
        &g_AudioState->apuLink_tx,
        APP_ALNK_TX_PIO,
        APP_ALNK_TX_SM,
        APP_ALNK_TX_DATA_CNT,
        APP_ALNK_TX_D0_PIN,
        APP_ALNK_TX_ACK_PIN,
        APP_ALNK_TX_IRQ,
        ALNK_DIV
    );
    g_AudioState->apuLink_tx.name = "apuLink_tx";
    bus_tx_init( &g_AudioState->apuLink_tx );

    bus_rx_configure(&g_AudioState->apuLink_rx,
        APP_ALNK_RX_PIO,
        APP_ALNK_RX_SM,       
        APP_ALNK_RX_DATA_CNT,
        APP_ALNK_RX_D0_PIN,
        APP_ALNK_RX_ACK_PIN
    );
    bus_rx_init(&g_AudioState->apuLink_rx);
     
    struct ApuClientInitOptions_t apuOptions = {0};
    apuOptions.apuLink_tx = apuLink_tx;
    apuOptions.apuLink_rx = apuLink_rx;        
    
    g_AudioState->client = apu_client_init(&apuOptions);
    if(!g_AudioState->client)
        return SDKErr_Fail;
    
    // alloc buffers
    g_AudioState->streamBufferSize = options->streamBufferSize;
    g_AudioState->audioWriteBufferStateSize = options->streamBufferSize + sizeof(APUCMD_WriteAudioEngineState);
    for(int i=0;i<NUM_ELEMS(g_AudioState->audioWriteStateOut);i++)
    {
        g_AudioState->audioWriteStateOut[i] = picocom_malloc(g_AudioState->audioWriteBufferStateSize);
        if(!g_AudioState->audioWriteStateOut[i])
            return SDKErr_Fail;
        memset(g_AudioState->audioWriteStateOut[i], 0, sizeof(APUCMD_WriteAudioEngineState));
    }

    return SDKErr_OK;
}


int audio_deinit()
{
    if(g_AudioState)
    {
        picocom_free(g_AudioState);
        g_AudioState = 0;
    }
    return SDKErr_OK;
}


int audio_reset_apu()
{
    if(!g_AudioState || !g_AudioState->client)
        return SDKErr_Fail;

    storage_reset();

    struct APUCMD_Reset cmd;
    BUS_INIT_CMD(cmd, EBusCmd_APU_Reset);             
    
    struct Res_APU_GetStatus response;    
    // NOTE: fp cache clean slow with no sdcard
#ifdef PICOCOM_NATIVE_SIM        
    int res = bus_tx_request_blocking_ex(g_AudioState->client->apuLink_tx, g_AudioState->client->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_AudioState->client->defaultTimeout+2500, test_service_audio_main, 0);
#else
    int res = bus_tx_request_blocking(g_AudioState->client->apuLink_tx, g_AudioState->client->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_AudioState->client->defaultTimeout+2500);    
#endif

    return res;
}


int audio_buffer_create(enum EAudioFileFormat format, uint32_t size, uint8_t sourceChannels, uint8_t isFlash)
{
    return audio_buffer_create_ex( format, size, sourceChannels, isFlash, 0, false );
}


int audio_buffer_create_ex(enum EAudioFileFormat format, uint32_t size, uint8_t sourceChannels, uint8_t isFlash, uint32_t uncompressedSize, bool isCompressed )
{
    if(!g_AudioState || !g_AudioState->client)
        return SDKErr_Fail;

    // alloc buffer id
    int freeClipId = -1;
    for(int i=0;i<AUDIO_MAX_AUDIO_CLIPS;i++)
    {
        if(!g_AudioState->audioClips[i].allocated)
        {
            freeClipId = i;
            break;
        }
    }
    if(freeClipId == -1)
        return SDKErr_Fail;

    // check buffer was created 
    struct AudioClipBuffer_t* clip = &g_AudioState->audioClips[freeClipId];
    if(clip->allocated)
        return SDKErr_Fail;

    struct APUCMD_CreateClipMem cmd;
    BUS_INIT_CMD(cmd, EBusCmd_APU_SND_CreateClipMem);             
    cmd.clipId = (int16_t)freeClipId;
    cmd.format = (uint8_t)format;
    cmd.size = isCompressed ? uncompressedSize : size;              
    cmd.sourceChannels = sourceChannels;
    cmd.flashBaseOffset = isFlash ? g_AudioState->lastFlashOffset : 0;
    cmd.isFlash = isFlash;         

    clip->allocated = 1;
    clip->isFlash = isFlash;
    clip->flashBaseOffset = cmd.flashBaseOffset;
    clip->isCompressed = isCompressed;

    if(isFlash)
    {
        uint32_t pageCount = CEIL_INT(size, FLASH_SECTOR_SIZE);
        if(pageCount <= 0)
            pageCount = 1;

        g_AudioState->lastFlashOffset += ( pageCount*FLASH_SECTOR_SIZE);
    }

    struct Res_int32 response;
#ifdef PICOCOM_NATIVE_SIM            
    int res = bus_tx_request_blocking_ex(g_AudioState->client->apuLink_tx, g_AudioState->client->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_AudioState->client->defaultTimeout, test_service_audio_main, 0);
#else
    int res = bus_tx_request_blocking(g_AudioState->client->apuLink_tx, g_AudioState->client->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_AudioState->client->defaultTimeout +10000000 );
#endif    
    if(res != SDKErr_OK)
        return -1;

    return cmd.clipId;
}


static int audio_buffer_upload_internal( uint16_t clipId, const uint8_t* buffer, uint32_t size, uint32_t dstOffset )
{
    if(!g_AudioState || !g_AudioState->client || clipId >= AUDIO_MAX_AUDIO_CLIPS || !buffer)
        return SDKErr_Fail;

    // check buffer was created 
    struct AudioClipBuffer_t* clip = &g_AudioState->audioClips[clipId];
    if(!clip->allocated)
        return SDKErr_Fail;

    // write blob as 512b blocks
    int remain = size;
    int readOffset = 0;

    // max send size aligned to flash pages
    int blockSz = g_AudioState->client->maxCmdSize;
    if(blockSz > size)
        blockSz = size;
    if(blockSz > APU_FLASH_BUFFER_PAGE_SIZE)
        blockSz = APU_FLASH_BUFFER_PAGE_SIZE;

    uint32_t pageSize = 0;
    int pageCnt = 0;
    while(remain > 0)
    {                 
        bool isLast = false;

        if(remain <= blockSz)
        {
            blockSz = remain;
            isLast = true;
        }

        struct APUCMD_ClipDataBlock cmd;
        BUS_INIT_CMD(cmd, EBusCmd_APU_SND_LoadClipMemBlock);             
        cmd.clipId = clipId;
        cmd.offset = readOffset + clip->flashBaseOffset + dstOffset;
        cmd.isLast = isLast;

        // write
        cmd.sz = blockSz;
        for(int i=0;i<blockSz;i++)
        {
            cmd.data[i] = buffer[readOffset+i];            
        }
        
        struct Res_int32 response;
#ifdef PICOCOM_NATIVE_SIM                    
        int res = bus_tx_request_blocking_ex(g_AudioState->client->apuLink_tx, g_AudioState->client->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_AudioState->client->defaultTimeout, test_service_audio_main, 0 );        
#else        
        int res = bus_tx_request_blocking(g_AudioState->client->apuLink_tx, g_AudioState->client->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_AudioState->client->defaultTimeout );        
#endif
        if(res != SDKErr_OK)
            return SDKErr_Fail;
        
        if(response.result != SDKErr_OK)
            return response.result;

        remain -= blockSz;            
        readOffset += blockSz;
        pageSize += blockSz; 
    }

    return SDKErr_OK;
}


static int audio_buffer_upload_compressed( uint16_t clipId, const uint8_t* buffer, uint32_t size )
{
    if(!g_AudioState || !g_AudioState->client || clipId >= AUDIO_MAX_AUDIO_CLIPS || !buffer)
        return SDKErr_Fail;

    // check buffer was created 
    struct AudioClipBuffer_t* clip = &g_AudioState->audioClips[clipId];
    if(!clip->allocated)
        return SDKErr_Fail;

    int res = SDKErr_Fail;

    uint32_t bufferOffset = 0;

    // read block count
    uint32_t blockCount;
    memcpy(&blockCount, buffer + bufferOffset, sizeof(blockCount));
    bufferOffset += sizeof(blockCount);

    // Block iter
    uint32_t readMemOffset = 0;
    for(int i=0;i<blockCount;i++)
    {
        uint16_t blockSize;
        memcpy(&blockSize, buffer + bufferOffset, sizeof(blockSize));
        bufferOffset += sizeof(blockSize);

        // zlib decompress                    
        mz_ulong uncomp_len = sizeof(pUncomp);
        int cmp_status = uncompress(pUncomp, &uncomp_len, buffer + bufferOffset, blockSize);
        if (cmp_status != Z_OK)
        {
            printf("compress() failed!\n");
            return SDKErr_Fail;
        }

        // Do normal upload
        res = audio_buffer_upload_internal( clip->clipId, pUncomp, uncomp_len, clip->flashBaseOffset + readMemOffset );             
        if(res != SDKErr_OK)    
            return res;

        // Next block
        bufferOffset += blockSize;
        readMemOffset += uncomp_len;
    }

    return res;
}


int audio_buffer_upload( uint16_t clipId, const uint8_t* buffer, uint32_t size )
{
    if(!g_AudioState || !g_AudioState->client || clipId >= AUDIO_MAX_AUDIO_CLIPS || !buffer)
        return SDKErr_Fail;

    // check buffer was created 
    struct AudioClipBuffer_t* clip = &g_AudioState->audioClips[clipId];
    if(!clip->allocated)
        return SDKErr_Fail;

    if( clip->isCompressed )
    {
        return audio_buffer_upload_compressed( clipId, buffer, size );
    }
    else
    {
        return audio_buffer_upload_internal( clipId, buffer, size, 0 );
    }
}


int audio_update()
{
    if(!g_AudioState || !g_AudioState->client)
        return SDKErr_Fail;

    struct ApuClientImpl_t* client = g_AudioState->client;
    bus_rx_update(client->apuLink_rx);
    bus_tx_update(client->apuLink_tx);

#ifdef PICOCOM_NATIVE_SIM  
    test_core_apu_full_update();
#endif    

    // Apu status query
    if(!bus_tx_is_busy(client->apuLink_tx))
    {     
        static struct Cmd_MultiAudioStatusSD multiCmd;
        ZERO_MEM(multiCmd);
        BUS_INIT_CMD(multiCmd, EBusCmd_APU_MultiAudioStatusSD);     

        // storage multiplexer               
        storage_audio_append_multi_cmd( &multiCmd );                     
        
        // write pending
        struct APUCMD_WriteAudioEngineState* audioState = audio_get_write_state();       // Current writing state
        if(audioState && audioState->count > 0)
        {                
            // Next state        
            g_AudioState->audioWriteStateOutBufferId++;

            // Prepare next
            APUCMD_WriteAudioEngineState* nextAudioState = audio_get_write_state();       // next state
            memset(nextAudioState, 0, sizeof(*nextAudioState));            
            nextAudioState->streamWriteBufferId = 0xffff;            
            nextAudioState->streamWriteBufferSz = 0;

            BUS_INIT_CMD_PTR(audioState, EBusCmd_APU_SND_WriteAudioEngineState);

            // add extra
            if(audioState->streamWriteBufferSz > 0)
            {
                // Ensure limit
                if(audioState->streamWriteBufferSz > g_AudioState->streamBufferSize)
                {
                    //printf("[FAIL] audioState->streamWriteBufferSz %d > g_AudioState->streamBufferSize %d\n", audioState->streamWriteBufferSz, g_AudioState->streamBufferSize);
                    return SDKErr_Fail;
                }

                // Add payload after cmd
                audioState->header.sz += audioState->streamWriteBufferSz ;
            }

            // copy state 
            // NOTE: this needs to be optimized for memory use, will have another copy of the audio state (3 in total now with multi cmd)
            memcpy( (uint8_t*)&multiCmd.audioState, audioState, sizeof(*audioState) );
        }
        
        // Get status
        {    
            static struct Cmd_APU_GetStatus* statusCmd = &multiCmd.getStatus;
            BUS_INIT_CMD_PTR(statusCmd, EBusCmd_APU_GetStatus);                 
            statusCmd->forceMediaCheck = false;
            statusCmd->clearHIDCounters = true;
        }

        multiCmd.header.crc = bus_calc_msg_crc(&multiCmd.header);

        // Send req        
        bus_tx_write_cmd_async(client->apuLink_tx, &multiCmd.header);
    }   

    return SDKErr_OK;
}


struct AudioState_t* audio_get_state()
{   
    return g_AudioState;
}


struct ApuClientImpl_t* audio_get_impl()
{
    if(!g_AudioState || !g_AudioState->client)
        return 0;

    return g_AudioState->client;
}


int audio_play(uint16_t clipId, uint32_t channelId, AudioClipPlayOptions_t* options)
{
    if(!g_AudioState || !g_AudioState->client)
        return SDKErr_Fail;

    int res;

    if(!g_AudioState)
        return SDKErr_Fail;

    if( channelId > AUDIO_MAX_AUDIO_CHANNELS)
        return SDKErr_Fail;

    struct APUCMD_WriteAudioEngineState* writingAudioState = audio_get_write_state();
    if(!writingAudioState)
        return SDKErr_Fail;

    // Find existing in out state
    struct AudioEngineChannelWriteStateData* foundState = 0;
    for(int i=0;i<writingAudioState->count;i++)
    {
        struct AudioEngineChannelWriteStateData* state = &writingAudioState->states[i];
        if(state->channelId == channelId)
        {
            foundState = state;
            break;
        }
    }

    // Find free
    if(!foundState)
    {
        if(writingAudioState->count < AUDIO_MAX_AUDIO_SYNC_STATES)
            foundState = &writingAudioState->states[ writingAudioState->count ];        
        writingAudioState->count++;
    }

    if(!foundState)
        return SDKErr_Fail;

    foundState->fieldBits = AudioEngineChannelWriteStateDataBits_all;
    foundState->channelId = channelId;    
    foundState->clipId = clipId;
    foundState->sampleOffset = options ? options->offset : 0.0f;
    foundState->rate = options ? options->rate : 1.0;
    foundState->volLeft = options ? options->volLeft : 1.0;
    foundState->volRight = options ? options->volRight : 1.0;
    foundState->loop = options ? options->loop : 0;
    if(options)
        foundState->playingState = options->paused ? ECmdChannelPlayState_Paused : ECmdChannelPlayState_Play;
    else
        foundState->playingState =  ECmdChannelPlayState_Play;
    foundState->playId = g_AudioState->lastPlayId++;

    return SDKErr_OK;
}


bool audio_is_playing(uint32_t channelId)
{
    if(!g_AudioState)
        return false;
        
    // Find existing in out state
    struct APUCMD_WriteAudioEngineState* writingAudioState = audio_get_write_state();
    if(writingAudioState)
    {
        struct AudioEngineChannelWriteStateData* foundState = 0;
        for(int i=0;i<writingAudioState->count;i++)
        {
            struct AudioEngineChannelWriteStateData* state = &writingAudioState->states[i];
            if(state->channelId == channelId)
            {
                foundState = state;
                break;
            }
        }
    }

    // Return last status cmd
    for(int i=0;i<g_AudioState->audioState.count;i++)
    {
        struct AudioEngineChannelPlaybackStateData* state = &g_AudioState->audioState.states[i];
        if(state->channelId == channelId)
        {
            return state->playingState == ECmdChannelPlayState_Play;            
        }
    }    

    return false;
}


float audio_get_play_offset(uint32_t channelId)
{
    // Find existing in out state
    struct APUCMD_WriteAudioEngineState* writingAudioState = audio_get_write_state();
    if(writingAudioState)
    {
        struct AudioEngineChannelWriteStateData* foundState = 0;
        for(int i=0;i<writingAudioState->count;i++)
        {
            struct AudioEngineChannelWriteStateData* state = &writingAudioState->states[i];
            if(state->channelId == channelId)
            {
                return state->sampleOffset;
            }
        }
    }

    // Return last status cmd
    for(int i=0;i<g_AudioState->audioState.count;i++)
    {
        struct AudioEngineChannelPlaybackStateData* state = &g_AudioState->audioState.states[i];
        if(state->channelId == channelId)
        {
            if(state->playingState == ECmdChannelPlayState_Play)
                return state->sampleOffset;
        }
    }    

    return 0.0f;
}


struct AudioClipPlayOptions_t audio_default_play_options()
{
    return (struct AudioClipPlayOptions_t){
        .volLeft = 1.0f,
        .volRight = 1.0f,
        .rate = 1.0f,
        .paused = false,
        .loop = false,
        .offset = 0.0f,
    };
}


void audio_post_audio_state(struct AudioEngineStateIn* audioState){
    if(!g_AudioState)   
        return;
    
    g_AudioState->audioState = *audioState;
    g_AudioState->audioStateInconsistent = false;
}


struct APUCMD_WriteAudioEngineState* audio_get_write_state()
{
    if(!g_AudioState)
        return 0;
    return g_AudioState->audioWriteStateOut[ ((g_AudioState->audioWriteStateOutBufferId + 0) % 2) ];
}


struct APUCMD_WriteAudioEngineState* audio_get_upload_state()
{
    return g_AudioState->audioWriteStateOut[ ((g_AudioState->audioWriteStateOutBufferId + 1) % 2) ];
}


static struct AudioEngineChannelWriteStateData* audio_find_next_channel_writing_state( uint32_t channelId, bool create)
{
    if(!g_AudioState || !g_AudioState->client)
        return 0;

    int res;

    if(!g_AudioState)
        return 0;

    if( channelId > AUDIO_MAX_AUDIO_CHANNELS)
        return 0;

    struct APUCMD_WriteAudioEngineState* writingAudioState = audio_get_write_state();
    if(!writingAudioState)
        return 0;

    // Find existing in out state
    struct AudioEngineChannelWriteStateData* foundState = 0;
    for(int i=0;i<writingAudioState->count;i++)
    {
        struct AudioEngineChannelWriteStateData* state = &writingAudioState->states[i];
        if(state->channelId == channelId)
        {
            foundState = state;
            break;
        }
    }

    if(!create)
        return 0;

    // Find free
    if(!foundState)
    {
        if(writingAudioState->count < AUDIO_MAX_AUDIO_SYNC_STATES)
        {
            foundState = &writingAudioState->states[ writingAudioState->count ];        
            writingAudioState->count++;

            memset(foundState, 0, sizeof(*foundState));
            foundState->channelId = channelId;
        }
    }

    if(!foundState)
        return 0;
    
    return foundState;
}


int audio_stop(uint32_t channelId)
{
    struct AudioEngineChannelWriteStateData* foundState = audio_find_next_channel_writing_state( channelId, true );
    if(!foundState)
        return 0;

    foundState->fieldBits = AudioEngineChannelWriteStateDataBits_clipId;
    foundState->channelId = channelId;    
    foundState->clipId = 0xffff;    
    foundState->playingState = ECmdChannelPlayState_None;
    
    return 1;
}


int audio_set_rate(uint32_t channelId, float rate)
{
    struct AudioEngineChannelWriteStateData* foundState = audio_find_next_channel_writing_state( channelId, true );
    if(!foundState)
        return 0;

    foundState->fieldBits |= AudioEngineChannelWriteStateDataBits_rate;
    foundState->channelId = channelId;    
    foundState->rate = rate;    
    
    return 1;
}


int audio_set_vol(uint32_t channelId, float volLeft, float volRight)
{
    struct AudioEngineChannelWriteStateData* foundState = audio_find_next_channel_writing_state( channelId, true );
    if(!foundState)
        return 0;

    foundState->fieldBits |= AudioEngineChannelWriteStateDataBits_volLeft | AudioEngineChannelWriteStateDataBits_volRight;
    foundState->channelId = channelId;    
    foundState->volLeft = volLeft;    
    foundState->volRight = volRight;    
    
    return 1;
}


int audio_clip_queue_add(uint32_t channelId, uint16_t* clipIds, uint8_t clipCnt)
{
    // Limit calls per update
    if(audio_has_pending_writing_state(channelId))
    {
        return 0;
    }

    // Find in out queue
    struct AudioEngineChannelWriteStateData* foundState = audio_find_next_channel_writing_state( channelId, true );
    if(!foundState)
        return 0;    

    foundState->fieldBits |= AudioEngineChannelWriteStateDataBits_addSeq;
    for(int i=0;i<clipCnt;i++)
    {
        if(foundState->clipSeqCnt >= NUM_ELEMS(foundState->clipSeq))
            return 0;
        foundState->clipSeq[ foundState->clipSeqCnt ] = clipIds[ i ];
        foundState->clipSeqCnt++;
    }

    return 1;
}


int audio_clip_queue_get_size(uint32_t channelId)
{
    // Find in out queue
    int baseCnt = 0;
    struct AudioEngineChannelWriteStateData* foundState = audio_find_next_channel_writing_state( channelId, false );
    if(foundState)
    {
        baseCnt += foundState->clipSeqCnt;
    }

    // Return last status cmd
    for(int i=0;i<g_AudioState->audioState.count;i++)
    {
        struct AudioEngineChannelPlaybackStateData* state = &g_AudioState->audioState.states[i];
        if(state->channelId == channelId)
        {
            baseCnt += state->clipSeqCnt; // may overflow
        }
    }    

    return baseCnt;
}


int audio_has_pending_writing_state(uint32_t channelId )
{
    struct AudioEngineChannelWriteStateData* foundState = audio_find_next_channel_writing_state( channelId, false );
    if(!foundState)
        return 0;

    return 1;
}


const struct AudioEngineChannelPlaybackStateData* audio_get_last_channel_state( uint32_t channelId )
{
    for(int i=0;i<g_AudioState->audioState.count;i++)
    {
        struct AudioEngineChannelPlaybackStateData* state = &g_AudioState->audioState.states[i];
        if(state->channelId == channelId)
        {
            return state;
        }
    }    
    return 0;
}
