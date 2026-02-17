#pragma once
#include "platform/pico/bus/bus.h"
#include "platform/pico/input/hw_hid_types.h"


//
// Config
#define APU_STATUS_PUBLISH_INTERVAL_US     30000            // default 30ms status publish interval
#define APU_AUDIO_STATE_PUBLISH_INTERVAL_US  30000          // default 30ms status publish interval
#define APU_HID_STATE_PUBLISH_INTERVAL_US  1000000          // default 1s status publish interval
#define APU_HID_STATE_MIN_PUBLISH_INTERVAL_US  30000          // min 30ms status publish interval
#define AUDIO_MAX_AUDIO_SYNC_STATES 32
#define APU_UPLOAD_BUFFER_BLOCK_SZ  512                     // Size of audio buffer upload blocks ( should be 4k flash page aligned )
#define APU_FLASH_BUFFER_PAGE_SIZE 4096                     // Flash buffers are fixed to 4k pages
#define APU_STORAGE_IO_WRITE_BLOCK_SZ   2048                // Write IO blocks 
#define APU_STORAGE_IO_READ_BLOCK_SZ   2048                // Write IO blocks 

/** Audio clip type */
enum EAudioDataFormat
{
    EAudioDataFormat_None,
    EAudioDataFormat_RawPCM,
    EAudioDataFormat_Ogg
};


/** Audio clip type */
enum EAudioDataSource
{
    EAudioDataSource_None,
    EAudioDataSource_Ram,                       // ptr to ram (RamFile)
    EAudioDataSource_File,                      // File pointer (std::File)
};


/** APU Bus commands
 */
enum EBusCmd_APU
{    
    // APU config
    EBusCmd_APU_GetStatus = EBusCmd_APU_BASE,        
    EBusCmd_APU_GetConfig,
    EBusCmd_APU_SetConfig,     
    EBusCmd_APU_Reset,   
    EBusCmd_APU_GetHIDState,
    EBusCmd_APU_MultiAudioStatusSD,
    // Audio             
    EBusCmd_APU_SND_LoadClipFile, 
    EBusCmd_APU_SND_CreateClipMem,
    EBusCmd_APU_SND_LoadClipMemBlock, 
    EBusCmd_APU_SND_UnloadClip,
    EBusCmd_APU_SND_GetClips, 
    EBusCmd_APU_SND_ReadAudioEngineState, 
    EBusCmd_APU_SND_WriteAudioEngineState, 
    EBusCmd_APU_SND_GetAudioState,
    // HID
    EBusCmd_APU_HID_GetHIDState,     // [refactor] case
    // SD IO    
    EBusCmd_APU_SD_OpenFile,        // Open file handle on SD card    
    EBusCmd_APU_SD_CloseFile,       // Close file handle on SD card    
    EBusCmd_APU_SD_OpenDir,         // Open dir handle on SD card    
    EBusCmd_APU_SD_ListDirNext,     // List next dir item
    EBusCmd_APU_SD_FileStat,        // File stat
    EBusCmd_APU_SD_ReadFile,        // Read file block
    EBusCmd_APU_SD_WriteFile,       // Write file block
    EBusCmd_APU_SD_MoveFile,        // Move file
    EBusCmd_APU_SD_UnlinkFile,      // Delete file    
    EBusCmd_APU_SD_FileStatAsync,   // File stat (async)
    EBusCmd_APU_SD_OpenFileAsync,   /// File open (async)
    EBusCmd_APU_SD_ReadFileAsync,
    EBusCmd_APU_SD_WriteFileAsync,
    EBusCmd_APU_SD_CloseFileAsync,
    // Boot
    EBusCmd_APU_UTIL_setNextBootInfo,// [refactor] case
    EBusCmd_APU_UTIL_getNextBootInfo,// [refactor] case
};


/** APU channel playback state */
typedef struct __attribute__((__packed__)) AudioEngineChannelPlaybackStateData
{    
    uint8_t channelId;          // channel index     
    float sampleOffset;         // Current sample offset in playback ( sub sample offsets for pitching )    
    uint8_t playingState;       // Channel play state  (ECmdChannelPlayState)
    uint32_t playId;            // Unique play id
    uint8_t clipSeqCnt;
    uint16_t clipSeq[8];        
} AudioEngineChannelPlaybackStateData;


/** APU read playback state */
typedef struct __attribute__((__packed__)) AudioEngineStateIn
{
    Cmd_Header_t header;     
    uint16_t count;     
    struct AudioEngineChannelPlaybackStateData states[AUDIO_MAX_AUDIO_SYNC_STATES]; 
} AudioEngineStateIn;


/** APU get status */
typedef struct __attribute__((__packed__)) Cmd_APU_GetStatus
{    
    Cmd_Header_t header;      
    bool forceMediaCheck;
    bool clearHIDCounters;
} Cmd_APU_GetStatus;


/** APU get status result */
typedef struct __attribute__((__packed__)) Res_APU_GetStatus
{    
    Cmd_Header_t header;    
    uint32_t last_audioIOTime;
    uint32_t last_audioRenderTime;
    uint32_t last_tickTime;
    HIDCMD_getState HIDState;
    AudioEngineStateIn audioState;
    bool hasAudioState;
    bool hasHIDState;
    bool hasAudio;
    bool hasSDMounted;      // fs mounted
    bool hasSDDetected;     // card in slot
    struct Res_Bus_Diag_Stats bus_stats;    // BusIO stats 
    uint16_t lastAsyncIOTxSeqNum;
} Res_APU_GetStatus;


/** APU get HID state */
typedef struct __attribute__((__packed__)) Cmd_APU_HIDState
{    
    Cmd_Header_t header;          
    bool clearHIDCounters;
} Cmd_APU_HIDState;


/** APU get HID state result */
typedef struct __attribute__((__packed__)) Res_APU_HIDState
{    
    Cmd_Header_t header;    
    int32_t result;
    struct HIDCMD_getState hidState;
} Res_APU_HIDState;



/** APU config */
typedef struct __attribute__((__packed__)) APUCMD_Config
{    
    uint32_t statusUpdateIntervalUs;            // Status publish interval (default APU_STATUS_PUBLISH_INTERVAL_US)    
    uint32_t statusUpdateAudioStateUs;          // Audio state publish interval (default APU_AUDIO_STATE_PUBLISH_INTERVAL_US)    
    uint8_t audioOutputWriteEnabled;            // Enable Audio output writer
    uint32_t hidUpdateIntervalUs;               // Hid push interval
    uint32_t hidUpdateMinIntervalUs;            // HID min status interval
    Cmd_Header_t header;    
} APUCMD_Config;


/** Reset apu state */
typedef struct __attribute__((__packed__)) APUCMD_Reset
{    
    Cmd_Header_t header;    
} APUCMD_Reset;


/** Create clip for mem upload -> Res_int32 res code */
typedef struct __attribute__((__packed__)) APUCMD_CreateClipMem
{    
    Cmd_Header_t header;    
    uint16_t clipId;            // unique clip id slot 
    uint8_t format;             // clip format (EAudioFileFormat)
    uint32_t size;              // Clip size
    uint8_t sourceChannels;     // Clip source channels ( if loading raw / no headers )
    uint32_t flashBaseOffset;   // Offset from XIP_BASE-arenaSize
    uint8_t isFlash;            // Upload to flash storage
} APUCMD_CreateClipMem;


/** Load audio clip -> Res_int32 res code */
typedef struct __attribute__((__packed__)) APUCMD_loadClipFile
{    
    Cmd_Header_t header;    
    uint16_t clipId;            // unique clip id slot 
    uint8_t format;             // clip format (EAudioFileFormat)
    uint8_t sourceChannels;     //  clip source channels ( if loading raw / no headers )
    uint8_t filename[64];       // filename to load (string)
} APUCMD_loadClipFile;


/** Load audio clip data chunk unti APU memory */
typedef struct __attribute__((__packed__)) APUCMD_ClipDataBlock
{    
    Cmd_Header_t header;     
    uint16_t clipId;            
    uint32_t offset;
    uint32_t sz;
    uint8_t isLast;                 // Commit flash block
    uint8_t data[APU_UPLOAD_BUFFER_BLOCK_SZ];
} APUCMD_ClipDataBlock;


/** Channel write state data */
typedef struct __attribute__((__packed__)) AudioEngineChannelWriteStateData
{    
    uint8_t channelId;              // channel index 
    uint32_t fieldBits;             // Fields enabled (when writing in order of values eg. 1 << 0 == clipId etc )
    uint16_t clipId;                // clip Id
    float sampleOffset;             // Current sample offset in playback ( sub sample offsets for pitching )
    float rate;                     // sampler step / pitch playback speed, 1.0 for normal speed
    float volLeft;                  // volume left
    float volRight;                 // volume right
    uint8_t loop;                   // Loop on eof
    uint8_t playingState;           // Channel playing state
    uint32_t playId;                // Play id
    uint8_t clipSeqCnt;
    uint16_t clipSeq[8];    
} AudioEngineChannelWriteStateData;


/** AudioEngineChannelWriteStateData fieldBits*/
enum EAudioEngineChannelWriteStateDataBits
{
    AudioEngineChannelWriteStateDataBits_clipId = (1 << 0),
    AudioEngineChannelWriteStateDataBits_sampleOffset = (1 << 1),
    AudioEngineChannelWriteStateDataBits_rate = (1 << 2),
    AudioEngineChannelWriteStateDataBits_volLeft = (1 << 3),
    AudioEngineChannelWriteStateDataBits_volRight = (1 << 4),
    AudioEngineChannelWriteStateDataBits_loop = (1 << 5),    
    AudioEngineChannelWriteStateDataBits_reverved0 = (1 << 6),   // [reverved]
    AudioEngineChannelWriteStateDataBits_playId = (1 << 7), 
    AudioEngineChannelWriteStateDataBits_offset = (1 << 8), 
    AudioEngineChannelWriteStateDataBits_addSeq = (1 << 9), 
    AudioEngineChannelWriteStateDataBits_all = 0xffff
};


/** State to sync with APU */
typedef struct __attribute__((__packed__)) APUCMD_WriteAudioEngineState
{    
    Cmd_Header_t header;     
    uint16_t count; 
    struct  AudioEngineChannelWriteStateData states[AUDIO_MAX_AUDIO_SYNC_STATES];
    uint16_t streamWriteBufferId;
    uint32_t streamWriteBufferSz;
    uint8_t streamData[0];
} APUCMD_WriteAudioEngineState;


/** Play state */
enum ECmdChannelPlayState
{
    ECmdChannelPlayState_None,
    ECmdChannelPlayState_Play,
    ECmdChannelPlayState_Paused
};


/** APU read playback state */
typedef struct __attribute__((__packed__)) APUCMD_ReadAudioEngineState
{
    Cmd_Header_t header;     
    AudioEngineStateIn audioState;
} APUCMD_ReadAudioEngineState;


/** SD Open file handle */
typedef struct __attribute__((__packed__)) Cmd_SD_OpenFile
{    
    Cmd_Header_t header;     
    uint8_t mode;
    char filename[256];    
} Cmd_SD_openFile;


/** SD Open file result */
typedef struct __attribute__((__packed__)) Res_SD_OpenFile
{    
    Cmd_Header_t header;     
    uint8_t result;
    uint64_t fileHandle;
    uint32_t size;    
} Res_SD_openFile;


/** SD close file or dir */
typedef struct __attribute__((__packed__)) Cmd_SD_CloseFile
{    
    Cmd_Header_t header;     
    uint64_t fileHandle;
} Cmd_SD_CloseFile;


/** SD close file or dir */
typedef struct __attribute__((__packed__)) Res_SD_CloseFile
{    
    Cmd_Header_t header;     
    uint8_t result;
} Res_SD_CloseFile;


/** SD List next dir */
typedef struct __attribute__((__packed__)) Cmd_SD_ListDirNext
{    
    Cmd_Header_t header;     
    uint64_t fileHandle;
} Cmd_SD_ListDirNext;


/** SD next file result */
typedef struct __attribute__((__packed__)) Res_SD_ListDirNext
{    
    Cmd_Header_t header;     
    uint8_t result;
    bool isEof;
    char filename[256];  
    uint32_t size;
    uint32_t date;
    uint32_t time;
    uint32_t attrib;    
    uint8_t fileType;
} Res_SD_ListDirNext;


/** SD file info */
typedef struct __attribute__((__packed__)) Cmd_SD_FileStat
{    
    Cmd_Header_t header;     
    uint8_t mode;
    char filename[256];     
} Cmd_SD_FileStat;


/** SD file info result */
typedef struct __attribute__((__packed__)) Res_SD_FileStat
{    
    Cmd_Header_t header;     
    uint8_t result;
    uint32_t size;
    uint32_t date;
    uint32_t time;
    uint32_t attrib;    
    uint8_t fileType;
} Res_SD_FileStat;


/** SD file read */
typedef struct __attribute__((__packed__)) Cmd_SD_ReadFile
{
    Cmd_Header_t header;     
    uint64_t fileHandle;  
    uint32_t offset;
    uint32_t size;
} Cmd_SD_ReadFile;


/** SD file read result */
typedef struct __attribute__((__packed__)) Res_SD_ReadFile
{    
    Cmd_Header_t header;     
    uint8_t result;
    uint32_t offset;
    bool isEof;
    uint32_t size;
    uint8_t buffer[APU_STORAGE_IO_READ_BLOCK_SZ];
} Res_SD_ReadFile;


/** SD file read */
typedef struct __attribute__((__packed__)) Cmd_SD_WriteFile
{
    Cmd_Header_t header;     
    uint64_t fileHandle;  
    uint32_t offset;
    uint32_t size;
    uint8_t buffer[APU_STORAGE_IO_WRITE_BLOCK_SZ];    
} Cmd_SD_WriteFile;


/** SD file read result */
typedef struct __attribute__((__packed__)) Res_SD_WriteFile
{    
    Cmd_Header_t header;     
    uint8_t result;    
    uint32_t size;    
    uint32_t asyncIoReqId;
} Res_SD_WriteFile;


/** SD move read */
typedef struct __attribute__((__packed__)) Cmd_SD_MoveFile
{
    Cmd_Header_t header;     
    bool overwrite;
    char from[256];     
    char to[256];         
} Cmd_SD_MoveFile;


/** SD file move result */
typedef struct __attribute__((__packed__)) Res_SD_MoveFile
{    
    Cmd_Header_t header;     
    uint8_t result;     
} Res_SD_MoveFile;


/** SD file ulink */
typedef struct __attribute__((__packed__)) Cmd_SD_UnlinkFile
{
    Cmd_Header_t header;     
    char filename[256];     
} Cmd_SD_UnlinkFile;


/** SD file ulink result */
typedef struct __attribute__((__packed__)) Res_SD_UnlinkFile
{    
    Cmd_Header_t header;     
    uint8_t result;     
} Res_SD_UnlinkFile;


/** Apu multi command
 */
typedef struct __attribute__((__packed__)) Cmd_MultiAudioStatusSD
{
    Cmd_Header_t header;        

    // Status
    Cmd_APU_GetStatus getStatus;

    // Audio
    APUCMD_WriteAudioEngineState audioState;

    // IO
    uint8_t ioCmd;
    uint32_t asyncIoReqId;
    union {    
        struct Cmd_SD_OpenFile openCmd;        
        struct Cmd_SD_ReadFile readCmd;        
        struct Cmd_SD_WriteFile writeCmd;          
        struct Cmd_SD_CloseFile closeCmd;        
        struct Cmd_SD_FileStat statCmd;        
    };        

} Cmd_MultiAudioStatusSD;


/** Apu multi result
 */
typedef struct __attribute__((__packed__)) Res_MultiAudioStatusSD
{
    Cmd_Header_t header;        
    struct Res_APU_GetStatus getStatus;

    // IO
    uint8_t ioCmd;
    uint32_t asyncIoReqId;
    union {    
        struct Res_SD_OpenFile openCmd;        
        struct Res_SD_ReadFile readCmd;        
        struct Res_SD_WriteFile writeCmd;          
        struct Res_SD_CloseFile closeCmd;        
        struct Res_SD_FileStat statCmd;  
    }; 

} Res_MultiAudioStatusSD;
