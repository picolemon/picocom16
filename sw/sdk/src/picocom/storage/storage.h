/** Storage interface
*/
#pragma once
#include "picocom/platform.h"
#include "lib/platform/pico/bus/bus.h"
#include "platform/pico/apu/hw_apu_types.h"
 

// Constants
#define STORAGE_MAX_FILE_HANDLES 32                // Max storage open file handles
#define STORAGE_MAX_DIR_HANDLES 8                  // Max storage open file handles

// Fwd
struct ApuClientImpl_t;

/** File IO errors */
enum EFileIOError
{
    EFileIOError_None = 0,
    EFileIOError_General,
    EFileIOError_TooManyFileHandles,
    EFileIOError_InvalidFileHandle,
    EFileIOError_DiskIOError,             // Low level disc IO error, device needs re-init
    EFileIOError_NoPath,                    // Could not find the path
    EFileIOError_ReadOverflow,              // Read size too big for response
    EFileIOError_WriteProtected,            // Readonly fs
    EFileIOError_WriteDiskFull,             // Write failed to complete
};


/* File mode */
enum EFileMode {
    EFileMode_Read = 0x01,
    EFileMode_Write = 0x02,
    EFileMode_OpenExisting = 0x00,
    EFileMode_CreateNew = 0x04,
    EFileMode_CreateAlways = 0x08,
    EFileMode_OpenAlways = 0x10,
    EFileMode_OpenAppend = 0x30,
};


/* File type */
enum EFileType {
    EFileType_Other,
    EFileType_File,
    EFileType_Dir
};


/** File info */
typedef struct FileInfo_t
{
    char filename[256];
    enum EFileType fileType;
    uint32_t size;
} FileInfo_t;


/** File handle */
typedef struct FileHandle_t
{
    bool isOpen;
    bool isDir;
    uint64_t fileId;
    uint32_t offset;
} FileHandle_t;

#define DirHandle_t FileHandle_t


/** Storage system init options
*/
typedef struct StorageOptions_t {
    int enabled;
    struct ApuClientImpl_t* client;    
} StorageOptions_t;


/** Storage state */
typedef struct StorageState_t
{	
    struct BusTx_t* apuLink_tx;
    struct BusRx_t* apuLink_rx;    
	struct ApuClientImpl_t* client;	
    struct FileHandle_t fileHandles[STORAGE_MAX_FILE_HANDLES];    
    uint32_t lastAsyncReqUid;           // req id alloc

    struct Cmd_Header_t* nextAsyncCmd;    // queue next cmd
    uint32_t nextAsyncIoReqId;
    struct Cmd_Header_t* pendingAsyncCmd; // queued waiting completion    
    struct Cmd_Header_t* completionCmd;
    uint32_t completionAsyncReqId;      // Async id tagged for completion
    uint32_t completionCmdSz;           // Allocated size of completion cmd
    bool asyncComplete;    
    uint32_t asyncQueueTime;
    bool asyncCrcFail;
} StorageState_t;


// storage api
struct StorageOptions_t storage_default_options(struct ApuClientImpl_t* apuClient); // Get storage default options
struct StorageState_t* storage_get();
int storage_init_with_options(struct StorageOptions_t* options);                    // Init storage api
int storage_deinit();                                                               // shutdown audio interface and release state
int storage_update();                                                               // Update storage
struct FileHandle_t* storage_open(const char* filename, int mode);
void storage_reset();
int storage_stat(const char* filename, struct FileInfo_t* fileInfo);
int storage_read(struct FileHandle_t* fp, uint8_t* buffer, uint32_t sz);
int storage_write(struct FileHandle_t* fp, uint8_t* buffer, uint32_t sz);
int storage_close(struct FileHandle_t* fp);
int storage_move(const char* from, const char* to, bool overwrite);
struct DirHandle_t* storage_list_dir_open(const char* dir);
int storage_list_dir_next(struct DirHandle_t* fp, struct FileInfo_t* fileInfo);
int storage_list_dir_close(struct DirHandle_t* fp);
int storage_unlink(const char* filename);
int storage_available();
int storage_handle_async_response( Cmd_Header_t* frame, uint32_t asyncIoReqId );
int storage_set_next_io_cmd( struct Cmd_Header_t* cmd, uint32_t asyncIoReqId );
int storage_async_waiting_completion(  );
int storage_async_complete();
int storage_next_req_id();
struct Cmd_Header_t* storage_get_completion_result( bool clear );
struct Cmd_Header_t* storage_get_completion_result_expect( uint8_t cmd, uint32_t sz, uint32_t ioReqId, bool clear );
int storage_audio_append_multi_cmd( struct Cmd_MultiAudioStatusSD* multiCmd );
void storage_clear_async_state();
bool storage_has_state();
bool storage_async_timeout();