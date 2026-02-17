#include "storage.h"
#include "picocom/devkit.h"
#include "picocom/audio/audio.h"      // DEP: storage runs on APU
#include "components/apu_core/apu_client.h"
#include <stdio.h>
#ifdef PICOCOM_SDL
#include "lib/components/mock_hardware/queue.h"
#include "lib/components/mock_hardware/mutex.h"
#else
#include "pico/util/queue.h"
#include <pico/mutex.h>
#endif



// Globals
struct StorageState_t* g_StorageState = 0;
void test_service_storage_main(void* userData);

//
//
struct StorageOptions_t storage_default_options(struct ApuClientImpl_t* apuClient)
{
    struct StorageOptions_t options = (struct StorageOptions_t){
        .enabled = true,
        .client = apuClient,
    };
    return options;
}


int storage_init_with_options(struct StorageOptions_t* options)
{
    if(g_StorageState)
        return SDKErr_Fail;
    
    if(!options->enabled)
        return SDKErr_OK;

    g_StorageState = (struct StorageState_t*)picocom_malloc(sizeof(struct StorageState_t));
    memset(g_StorageState, 0, sizeof(StorageState_t));

    g_StorageState->client = options->client;
    g_StorageState->apuLink_tx = options->client->apuLink_tx;
    g_StorageState->apuLink_rx = options->client->apuLink_rx;

    return SDKErr_OK;
}


int storage_deinit()
{
    if(!g_StorageState)
    {
        picocom_free(g_StorageState);
        g_StorageState = 0;
    }
    
    return SDKErr_OK;
}


int storage_update()
{
    return SDKErr_OK;
}


FileHandle_t* allocFileHandle()
{
    if(!g_StorageState)
        return 0;
    
    for(int i=0;i<NUM_ELEMS(g_StorageState->fileHandles);i++)
    {
        if(!g_StorageState->fileHandles[i].isOpen)
        {            
            return &g_StorageState->fileHandles[i];
        }
    }
    
    return 0;
}


int freeFileHandle(struct FileHandle_t* handle)
{
    if(!g_StorageState)
        return SDKErr_Fail;

    for(int i=0;i<NUM_ELEMS(g_StorageState->fileHandles);i++)
    {
        if(&g_StorageState->fileHandles[i] == handle)
        {
            g_StorageState->fileHandles[i].isOpen = false;                
            return 1;
        }
    }

    return 0;    
}


void storage_reset()
{
    if(g_StorageState)
        memset(g_StorageState->fileHandles,0, sizeof(g_StorageState->fileHandles));
}


struct FileHandle_t* storage_open(const char* filename, int mode)
{
    if(!g_StorageState || !filename)
        return 0;
    
    // call rpc
    struct Cmd_SD_OpenFile cmd;
    strncpy(cmd.filename, filename,  sizeof(cmd.filename));
    cmd.mode = mode;
    BUS_INIT_CMD(cmd, EBusCmd_APU_SD_OpenFile);             
    
    struct Res_SD_OpenFile response;            

#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif

    // get file handle
    if(res != SDKErr_OK)
        return 0;
    
    if(response.result != EFileIOError_None)
        return 0;   

    FileHandle_t* handle = allocFileHandle();
    if(!handle)
        return 0;

    // lock alloc
    handle->fileId = response.fileHandle;
    handle->isOpen = true;
    handle->offset = 0;

    return handle;
}


int storage_stat(const char* filename, struct FileInfo_t* fileInfo)
{
    if(!g_StorageState || !fileInfo || !fileInfo)
        return 0;

    memset(fileInfo, 0, sizeof(*fileInfo));

    // call rpc
    struct Cmd_SD_FileStat cmd;    
    strncpy(cmd.filename, filename,  sizeof(cmd.filename));
    BUS_INIT_CMD(cmd, EBusCmd_APU_SD_FileStat);             
    
    struct Res_SD_FileStat response;    

#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif

    // get file handle
    if(res != SDKErr_OK)
        return 0;
    
    if(response.result != EFileIOError_None)
        return 0; 

    // copy result info or eof
    fileInfo->size = response.size;    
    strncpy(fileInfo->filename, filename, MIN(sizeof(fileInfo->filename), sizeof(cmd.filename)));

    return 1;
}


int storage_read(struct FileHandle_t* fp, uint8_t* buffer, uint32_t sz)
{
    if(!g_StorageState || !fp)
        return 0;
    
    uint32_t remain = sz;
    uint32_t writeOffset = 0;
    int readSize = 0;
    while(remain > 0)
    {
        // call rpc
        struct Cmd_SD_ReadFile cmd;    
        struct Res_SD_ReadFile response;
        cmd.fileHandle = fp->fileId;
        cmd.offset = fp->offset;
        cmd.size = remain;
        if(cmd.size > sizeof(response.buffer))
            cmd.size = sizeof(response.buffer);
        BUS_INIT_CMD(cmd, EBusCmd_APU_SD_ReadFile);             
  
#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif        

        // get file handle
        if(res != SDKErr_OK)
            return res;
        
        if(response.result != EFileIOError_None)
            return SDKErr_Fail;   
        
        // offset fp
        fp->offset += response.size;

        if(response.isEof)
        {
            writeOffset += response.size;
            break;
        }

        // Null size
        if(!response.size && remain > 0)
        {
            return SDKErr_Fail;
        }

        if( writeOffset + response.size > sz)
            return SDKErr_Fail;    // overflow

        // copy into dest
        memcpy(buffer + writeOffset, response.buffer, response.size);

        // next block
        remain -= response.size;
        writeOffset += response.size;;
    }

    return writeOffset;
}


int storage_write(struct FileHandle_t* fp, uint8_t* buffer, uint32_t sz)
{
    if(!g_StorageState || !fp)
        return 0;
    
    uint32_t remain = sz;
    uint32_t readOffset = 0;
    int readSize = 0;
    while(remain > 0)
    {
        // call rpc
        struct Cmd_SD_WriteFile cmd;    
        BUS_INIT_CMD(cmd, EBusCmd_APU_SD_WriteFile);                     

        struct Res_SD_WriteFile response;
        cmd.fileHandle = fp->fileId;
        cmd.offset = fp->offset;
        cmd.size = remain;
        
        if(cmd.size > sizeof(cmd.buffer))
            cmd.size = sizeof(cmd.buffer);

        if( readOffset + cmd.size > sz)
            return SDKErr_Fail;    // overflow

        // copy into dest
        memcpy(cmd.buffer, buffer + readOffset, cmd.size);

#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif        

        // get file handle
        if(res != SDKErr_OK)
            return res;
        
        if(response.result != EFileIOError_None)
            return SDKErr_Fail;   
        
        // offset fp
        fp->offset += response.size;

        // Null size
        if(!response.size && remain > 0)
        {
            return SDKErr_Fail;
        }

        // next block
        remain -= response.size;
        readOffset += response.size;;
    }

    return readOffset;
}


int storage_close(struct FileHandle_t* fp)
{
    if(!g_StorageState || !fp)
        return SDKErr_Fail;

    freeFileHandle(fp);

    // call rpc
    struct Cmd_SD_CloseFile cmd;    
    cmd.fileHandle = fp->fileId;
    BUS_INIT_CMD(cmd, EBusCmd_APU_SD_CloseFile);             
    
    struct Res_SD_CloseFile response;
#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif    

    // get file handle
    if(res != SDKErr_OK)
        return res;
    
    if(response.result != EFileIOError_None)
        return SDKErr_Fail;   

    return SDKErr_OK;
}


int storage_move(const char* from, const char* to, bool overwrite)
{

    if(!g_StorageState)
        return SDKErr_Fail;

    // call rpc
    struct Cmd_SD_MoveFile cmd;   
    
    if(strlen(from) > sizeof(cmd.from)-1)
        return SDKErr_Fail;
    if(strlen(to) > sizeof(cmd.to)-1)
        return SDKErr_Fail;        
    
    
    BUS_INIT_CMD(cmd, EBusCmd_APU_SD_MoveFile);        
    cmd.overwrite = overwrite;     
    strncpy(cmd.from, from,  sizeof(cmd.from));    
    strncpy(cmd.to, to,  sizeof(cmd.to));    

    struct Res_SD_MoveFile response;

#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif    

    // get file handle
    if(res != SDKErr_OK)
        return res;
    
    if(response.result != EFileIOError_None)
        return SDKErr_Fail;   

    return SDKErr_OK;
}


struct DirHandle_t* storage_list_dir_open(const char* dir)
{
    if(!g_StorageState || !dir)
        return 0;

    struct DirHandle_t* handle = (DirHandle_t*)allocFileHandle();
    if(!handle)
        return 0;

    // call rpc
    struct Cmd_SD_OpenFile cmd;
    strncpy(cmd.filename, dir,  sizeof(cmd.filename));    
    BUS_INIT_CMD(cmd, EBusCmd_APU_SD_OpenDir);             
    
    struct Res_SD_OpenFile response;

#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif    

    // get file handle
    if(res != SDKErr_OK)
        return 0;
    
    if(response.result != EFileIOError_None)
        return 0;   

    // lock alloc
    handle->fileId = response.fileHandle;
    handle->isOpen = true;
    handle->isDir = true;

    return handle;
}


int storage_list_dir_next(struct DirHandle_t* fp, struct FileInfo_t* fileInfo)
{
    if(!g_StorageState || !fileInfo || !fp)
        return 0;

    // call rpc
    struct Cmd_SD_ListDirNext cmd;    
    cmd.fileHandle = fp->fileId;
    BUS_INIT_CMD(cmd, EBusCmd_APU_SD_ListDirNext);             
    
    struct Res_SD_ListDirNext response;

#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif    

    // get file handle
    if(res != SDKErr_OK)
        return 0;
    
    if(response.result != EFileIOError_None)
        return 0; 

    if(response.isEof)  
        return 0;

    // copy result info or eof
    fileInfo->size = response.size;    
    strncpy(fileInfo->filename, response.filename, MIN(sizeof(fileInfo->filename), sizeof(response.filename)));
    fileInfo->fileType = response.fileType;
    
    return 1;
}


int storage_list_dir_close(struct DirHandle_t* fp)
{
    // Same handle storage
    return storage_close(fp);
}


int storage_unlink(const char* filename)
{
    if(!g_StorageState)
        return SDKErr_Fail;

    // call rpc
    struct Cmd_SD_UnlinkFile cmd;   
    
    if(strlen(filename) > sizeof(cmd.filename)-1)
        return SDKErr_Fail;

    
    BUS_INIT_CMD(cmd, EBusCmd_APU_SD_UnlinkFile);             
    strncpy(cmd.filename, filename,  sizeof(cmd.filename));    
    
    struct Res_SD_UnlinkFile response;

#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif    

    // get file handle
    if(res != SDKErr_OK)
        return res;
    
    if(response.result != EFileIOError_None)
        return SDKErr_Fail;   

    return SDKErr_OK;
}


int storage_available()
{
    if(!g_StorageState)
        return 0;

    return g_StorageState->client->lastHasStorage;
}


int storage_available_blocking()
{
    if(!g_StorageState)
        return 0;

    // call rpc
    struct Cmd_APU_GetStatus cmd;       
    cmd.forceMediaCheck = true;
    BUS_INIT_CMD(cmd, EBusCmd_APU_GetStatus);                 
    struct Res_APU_GetStatus response;
    cmd.clearHIDCounters = 0;
    cmd.forceMediaCheck = 1;

#ifdef PICOCOM_NATIVE_SIM           
    int res = bus_tx_request_blocking_ex(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout, test_service_storage_main, 0);
#else
    int res = bus_tx_request_blocking(g_StorageState->apuLink_tx, g_StorageState->apuLink_rx, &cmd.header, &response.header, sizeof(response), g_StorageState->client->defaultTimeout );
#endif    

    // get file handle
    if(res != SDKErr_OK)
        return 0;

    return response.hasSDMounted;
}


struct StorageState_t* storage_get()
{
    return g_StorageState;
}


int storage_next_req_id()
{
    g_StorageState->lastAsyncReqUid++;
    return g_StorageState->lastAsyncReqUid;
}


int storage_set_next_io_cmd( struct Cmd_Header_t* cmd, uint32_t asyncIoReqId )
{
    g_StorageState->nextAsyncCmd = cmd;
    g_StorageState->nextAsyncIoReqId = asyncIoReqId;
    g_StorageState->asyncComplete = false;
#ifdef TRACE_FILEIO         
    printf("storage_set_next_io_cmd\n");
#endif    
    g_StorageState->asyncQueueTime = picocom_time_ms_32();
    return 1;
}


int storage_can_set_io( struct Cmd_Header_t* cmd )
{
    return g_StorageState->nextAsyncCmd == 0;
}


int storage_audio_append_multi_cmd( struct Cmd_MultiAudioStatusSD* multiCmd )
{
    if( !g_StorageState )
        return 0;
        
    // Check for queued io
    if( g_StorageState->nextAsyncCmd )
    {
        multiCmd->ioCmd = 0;

        // copy into relevant field that matches cmd (ideally this would be generic)
        bool isValid = true;
        switch( g_StorageState->nextAsyncCmd->cmd )
        {
            case EBusCmd_APU_SD_FileStatAsync:       
                if( g_StorageState->nextAsyncCmd->sz != sizeof(multiCmd->statCmd) )
                {
                    break;
                }
                memcpy( (uint8_t*)&multiCmd->statCmd, (uint8_t*)g_StorageState->nextAsyncCmd, g_StorageState->nextAsyncCmd->sz );
                break;
            case EBusCmd_APU_SD_OpenFileAsync:
                if( g_StorageState->nextAsyncCmd->sz != sizeof(multiCmd->openCmd) )
                {
                    break;
                }
                memcpy( (uint8_t*)&multiCmd->openCmd, (uint8_t*)g_StorageState->nextAsyncCmd, g_StorageState->nextAsyncCmd->sz );
                break;            
            case EBusCmd_APU_SD_ReadFileAsync:
                if( g_StorageState->nextAsyncCmd->sz != sizeof(multiCmd->readCmd) )
                {
                    break;
                }
                memcpy( (uint8_t*)&multiCmd->readCmd, (uint8_t*)g_StorageState->nextAsyncCmd, g_StorageState->nextAsyncCmd->sz );
                break;            
            case EBusCmd_APU_SD_WriteFileAsync:
                if( g_StorageState->nextAsyncCmd->sz != sizeof(multiCmd->writeCmd) )
                {
                    break;
                }
                memcpy( (uint8_t*)&multiCmd->writeCmd, (uint8_t*)g_StorageState->nextAsyncCmd, g_StorageState->nextAsyncCmd->sz );
                break;            
            case EBusCmd_APU_SD_CloseFileAsync:
                if( g_StorageState->nextAsyncCmd->sz != sizeof(multiCmd->closeCmd) )
                {
                    break;
                }
                memcpy( (uint8_t*)&multiCmd->closeCmd, (uint8_t*)g_StorageState->nextAsyncCmd, g_StorageState->nextAsyncCmd->sz );
                break;              
        }
        
        if( isValid )
        {
            if( g_StorageState->pendingAsyncCmd )
            {
                printf("storage_audio_append_multi_cmd() g_StorageState->pendingAsyncCmd is not cleared, async in inclean state!\n");
            }

            multiCmd->ioCmd = g_StorageState->nextAsyncCmd->cmd;
            multiCmd->asyncIoReqId = g_StorageState->nextAsyncIoReqId;
            g_StorageState->pendingAsyncCmd = g_StorageState->nextAsyncCmd;                   
        }
        else
        {
            picocom_panic(SDKErr_Fail, "Invalid async io cmd");
        }
        g_StorageState->nextAsyncCmd = 0;
        g_StorageState->nextAsyncIoReqId = 0;
        return 1;
    }

    return 0;
}


int storage_handle_async_response( Cmd_Header_t* frame, uint32_t asyncIoReqId )
{    
#ifdef TRACE_FILEIO       
    printf("storage_handle_async_response cmd: %d, sz: %d, asyncIoReqId: %d\n", frame->cmd, frame->sz, asyncIoReqId);
#endif

    // hopefully max packet size should reject large sizes, would be better to one time alloc this
    if( g_StorageState->completionCmdSz < frame->sz )
    {
        g_StorageState->completionCmd = picocom_relloc( g_StorageState->completionCmd, frame->sz );
        if(!g_StorageState->completionCmd)
        {
            picocom_panic( SDKErr_Fail, "g_StorageState->completionCmd alloc failed" );
        }
    }

    memcpy( g_StorageState->completionCmd, frame, frame->sz );
    g_StorageState->asyncComplete = true;
    g_StorageState->completionAsyncReqId = asyncIoReqId;
    
    return 1;
}


int storage_async_waiting_completion(  )
{
    return g_StorageState->pendingAsyncCmd != 0;
}


int storage_async_complete()
{
    return g_StorageState->asyncComplete;
}


struct Cmd_Header_t* storage_get_completion_result( bool clear )
{
    if( !g_StorageState->asyncComplete )
        return 0;

    struct Cmd_Header_t* result = g_StorageState->completionCmd;

    if( clear )
    {
        g_StorageState->asyncComplete = true;        
    }

    return result;
}


struct Cmd_Header_t* storage_get_completion_result_expect( uint8_t cmd, uint32_t sz, uint32_t ioReqId, bool clear )
{
    struct Cmd_Header_t* result = storage_get_completion_result( clear );
    if( !result )
        return 0;

    if( ioReqId != g_StorageState->completionAsyncReqId )
        return 0;
    
    if( result->cmd != cmd || result->sz != sz )
        return 0;

    return result;
}


void storage_clear_async_state()
{
#ifdef TRACE_FILEIO       
    printf("storage_clear_async_state\n");
#endif    
    g_StorageState->nextAsyncCmd = 0;
    g_StorageState->nextAsyncIoReqId = 0;
    g_StorageState->pendingAsyncCmd = 0;     
    g_StorageState->completionAsyncReqId = 0;
    g_StorageState->completionCmdSz = 0;
    g_StorageState->asyncCrcFail = false;
    if( g_StorageState->completionCmdSz && g_StorageState->completionCmd)
    {
        //memset(g_StorageState->completionCmd, 0, g_StorageState->completionCmdSz);
    }
    g_StorageState->asyncComplete = false; 
}


bool storage_has_state()
{
    return g_StorageState->nextAsyncCmd != 0
        || g_StorageState->nextAsyncIoReqId != 0
        ||  g_StorageState->pendingAsyncCmd != 0
        ||  g_StorageState->completionAsyncReqId != 0
        ||  g_StorageState->completionCmdSz != 0
        ||  g_StorageState->asyncComplete != false;
}

bool storage_async_timeout()
{
    if( g_StorageState->asyncCrcFail)
    {
        return true;
    }

    int dt = picocom_time_ms_32() - g_StorageState->asyncQueueTime;
    if( dt > 1000 )
        return true;

    return false;
}
