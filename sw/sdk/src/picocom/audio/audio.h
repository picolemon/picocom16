/** Audio processor unit interface
*/
#pragma once
#include "picocom/platform.h"
#include "lib/platform/pico/bus/bus.h"
#include "platform/pico/apu/hw_apu_types.h"


// Constants
#define AUDIO_MAX_AUDIO_CLIPS 48                // Max number of loaded clips
#define AUDIO_MAX_AUDIO_CHANNELS 32             // Max playing audio channels
#define AUDIO_MAX_CMD_WRITE_CHANNELS 32         // Max number of writes per frame
#define AUDIO_RAM_STORAGE_SIZE 1024*256         // Audio ram buffer size
#define AUDIO_FLASH_STORAGE_SIZE (1024*1024*4) - 524288    // Audio flash buffer size

#ifdef __cplusplus
extern "C" {
#endif


// Fwd
struct ApuClientImpl_t;


/** Options to play audio clip 
*/
typedef struct AudioClipPlayOptions_t {
    float volLeft;      // right volume 1.0 for normal volume
    float volRight;
    float rate;         // playback rate 1.0 for normal speed
    bool paused;        // start paused
    bool loop;          // loop audio        
    float offset;       // play offset
} AudioClipPlayOptions_t;


/** Audio file format
 */
enum EAudioFileFormat
{
    EAudioFileFormat_None = 0,
    EAudioFileFormat_RawPCM = 1,
    EAudioFileFormat_Ogg = 2    
};


/** Audio clip state
*/
typedef struct AudioEngineChannelPlaybackStateData AudioClipState;


/** Audio clip buffer
*/
typedef struct AudioClipBuffer_t {
    int allocated;
    int clipId;    
    uint8_t isFlash;
    uint32_t flashBaseOffset;
    bool isCompressed;
} AudioClipBuffer_t;


/** Audio state */
typedef struct AudioState_t
{	
    struct BusTx_t apuLink_tx;
    struct  BusRx_t apuLink_rx;
	struct ApuClientImpl_t* client;	
    struct AudioClipBuffer_t audioClips[AUDIO_MAX_AUDIO_CLIPS];     
    uint32_t streamBufferSize;                                  // Max stream block size
    uint32_t audioWriteBufferStateSize;                         // Alloc side for state ( APUCMD_WriteAudioEngineState + streamClipData )   
    struct APUCMD_WriteAudioEngineState* audioWriteStateOut[2]; // Pending audio writes per tick
    uint32_t audioWriteStateOutBufferId;                        // audioWriteStateOut[audioWriteStateOutBufferId] is pending, writing swaped for next writes
    struct AudioEngineStateIn audioState;                       // Last APU audio state
    uint32_t lastPlayId;
    bool audioStateInconsistent;                                     // Local state modified
    uint32_t lastFlashOffset;                                   // Flash alloc last offset
} AudioState_t;


// Internal apu
void audio_post_audio_state(struct AudioEngineStateIn* audioState);

// audio api
struct AudioOptions_t audio_default_options();// Get audio default options
int audio_init();                     		// Init audio api default
int audio_init_with_options(struct AudioOptions_t* options); // Init audio api
int audio_deinit();                         // shutdown audio interface and release state
int audio_reset_apu();						// Reset apu hw
int audio_update();                   		// Tick audio system, dispatch commands to apu etc
struct AudioState_t* audio_get_state();		// Get audio state
struct ApuClientImpl_t* audio_get_impl();	// APU Client implementation
int audio_buffer_create(enum EAudioFileFormat format, uint32_t size, uint8_t sourceChannels, uint8_t isFlash); // Create audio buffer, returns buffer id or -1 for error code
int audio_buffer_create_ex(enum EAudioFileFormat format, uint32_t size, uint8_t sourceChannels, uint8_t isFlash, uint32_t uncompressedSize, bool isCompressed ); // Create audio buffer, returns buffer id or -1 for error code
int audio_buffer_upload(uint16_t clipId, const uint8_t* buffer, uint32_t size ); // Upload data into audio buffer, returns SDKErr.
struct AudioClipPlayOptions_t audio_default_play_options(); // Create default play options
int audio_play(uint16_t clipId, uint32_t channelId, AudioClipPlayOptions_t* options); // play audio clip on channel
int audio_clip_queue_add(uint32_t channelId, uint16_t* clipIds, uint8_t clipCnt); // queue clip on playing channel
int audio_clip_queue_get_size(uint32_t channelId);                  // Get current clip size
bool audio_is_playing(uint32_t channelId);                          // Is audio channel playing 
float audio_get_play_offset(uint32_t channelId);                    // Get audio playback offset
struct APUCMD_WriteAudioEngineState* audio_get_write_state();       // Current writing state
struct APUCMD_WriteAudioEngineState* audio_get_upload_state();      // Current background upload state
int audio_stop(uint32_t channelId);                                 // stop audio
int audio_set_rate(uint32_t channelId, float rate);                 // set playing audio rate
int audio_set_vol(uint32_t channelId, float volLeft, float volRight); // set playing audio rate
int audio_has_pending_writing_state( uint32_t channelId );          // State pending write to apu, clears on new status
const struct AudioEngineChannelPlaybackStateData* audio_get_last_channel_state( uint32_t channelId ); // Get last channel state from APU ( this does not include pending writing )

#ifdef __cplusplus
}
#endif