#include "apu_core.h"
#include "picocom/devkit.h"
#include "audio_engine.h"
#include "platform/pico/apu/hw_apu_types.h"
#include "picocom/storage/storage.h"
#include "thirdparty/crc16/crc.h"
#include <assert.h>
#ifdef PICOCOM_SDL
#include "platform/sdl2/storage/sdl_storage_driver.h"
#include <unistd.h>
#endif
#ifdef __EMSCRIPTEN__
    #include "emscripten.h"
#endif

#define IMPL_EMBED_builtin_sounds
#include "resources/builtin_sounds.inl"

//#define TRACE_FILEIO

// hid driver
void hid_driver_init();
struct HIDCMD_getState* hid_driver_get_state(); 
void hid_driver_update();


#ifndef PICOCOM_NATIVE_SIM    

//
//
uint32_t sdToStorageError(FRESULT err)
{
  switch (err)
  {
  case FR_OK:
    return EFileIOError_None;  
  case FR_DISK_ERR:
    return EFileIOError_DiskIOError;      
  case FR_NO_PATH:
    return EFileIOError_NoPath;
  case FR_WRITE_PROTECTED:
    return EFileIOError_WriteProtected;    
  default:
    return EFileIOError_General;
  }  
}
#else
char tmpMouthPathA[1024];
char tmpMouthPathB[1024];
static uint64_t lastFileHandleId = 1;

const char* append_tmp_path_to_mount_dir( char* tmpBuff, const char* filename );
const char* append_tmp_to_dir( char* tmpBuff, struct apuFPcache_t* handle, const char* filename );
void sanity_check_path_prefix( const char* path );
bool is_path_prefix_valid( const char* path );

#endif


void update_audio_state_cmd( struct apu_t* apu, AudioEngineStateIn* readAudioState)
{
    // get entire state
    memset(readAudioState, 0, sizeof(*readAudioState));
    BUS_INIT_CMD_PTR(readAudioState, EBusCmd_APU_SND_ReadAudioEngineState); // init full size    
    
    readAudioState->count = 0;

    // Sync entire state
    for(int i=0;i<MaxChannels;i++)
    {
      struct AudioChannel_t* state = &apu->audio->m_Channels[i];

      readAudioState->states[readAudioState->count].channelId = i;      
      readAudioState->states[readAudioState->count].sampleOffset = state->sampleOffset;      
      readAudioState->states[readAudioState->count].playingState = state->playingState;      
      readAudioState->states[readAudioState->count].playId = state->playId;   
      readAudioState->states[readAudioState->count].clipSeqCnt = audio_get_channel_clip_seq_queue_size( state );

      // Next
      readAudioState->count++;
      if(readAudioState->count >= AUDIO_MAX_AUDIO_SYNC_STATES)
        break;
    }
}


void update_status_cmd(struct apu_t* apu, struct Res_APU_GetStatus* status, bool clearHIDCounters)
{
  // Audio state
  status->hasAudio = apu->audio->m_Inited;

  // Sdcard state / or backend storage state
  status->hasSDDetected = apu->sdCardDetected;
  status->hasSDMounted = apu->sdCardMounted;

#ifdef PICOCOM_NATIVE_SIM
  // Direct calling hid query to avoid bus delays
#else
  // HID state    
  struct HIDCMD_getState* hidState = hid_driver_get_state();
  if(hidState) 
  {
    status->HIDState = *hidState;

    // reset buffer counters
    if(clearHIDCounters)
    {
      hidState->mouseCnt = 0;
      hidState->keyboardCnt = 0;
      hidState->gamepadCnt = 0;
    }

    status->hasHIDState = true;
  }
  else
  {
    status->hasHIDState = false;
  }
#endif
  // Add audio state
  status->hasAudioState = true;
  update_audio_state_cmd( apu, &status->audioState);

  // Bus stats
  bus_tx_update_stats(apu->app_alnk_tx, &status->bus_stats);
  bus_rx_update_stats(apu->app_alnk_rx, &status->bus_stats);    

  status->lastAsyncIOTxSeqNum = apu->lastAsyncIOTxSeqNum;
}


int apu_write_audio_cmd( struct apu_t* apu, struct APUCMD_WriteAudioEngineState* cmd )
{
    // Bad cmd
    if( cmd->count >= NUM_ELEMS(cmd->states))
      return 0;

    for(int i=0;i<cmd->count;i++)
    {
        struct AudioEngineChannelWriteStateData* newState = &cmd->states[i];

        // Bad corrupt data
        if( newState->channelId >= NUM_ELEMS(apu->audio->m_Channels) )
            continue;

        AudioChannel_t* c = &apu->audio->m_Channels[ newState->channelId ];
        c->updateCount++;

        if(newState->fieldBits & AudioEngineChannelWriteStateDataBits_sampleOffset)  // sampleOffset
        {
            c->sampleOffset = newState->sampleOffset;
        }

        if(newState->fieldBits & AudioEngineChannelWriteStateDataBits_rate)  // rate
        {
            c->sampleStep = newState->rate; // TODO: translate this to hw
        }

        if(newState->fieldBits & AudioEngineChannelWriteStateDataBits_volLeft)  // volLeft
        {
            c->volLeft = newState->volLeft;
        }

        if(newState->fieldBits & AudioEngineChannelWriteStateDataBits_volRight)  // volRight
        {
            c->volRight = newState->volRight;
        }

        if(newState->fieldBits & AudioEngineChannelWriteStateDataBits_loop)  // loop
        {
            c->loop = newState->loop;
        }

        if(newState->fieldBits & AudioEngineChannelWriteStateDataBits_playId)  // playId
        {
            c->playId = newState->playId;
        }

        // check clip changes      
        // State (with new state) will be re-fed into play as state 
        if(newState->fieldBits & AudioEngineChannelWriteStateDataBits_clipId)  // clipId
        {      
            // stop current clip
            audio_engine_stop( apu->audio, newState->channelId );

            // Trigger play if clip is valid
            AudioClip_t* newClip = audio_engine_getClipById( apu->audio, newState->clipId );

            if( newClip && newClip->dataFormat != EAudioDataFormat_None )
            {
            // copy state
            AudioChannel_t nextChannelState = *c;
            nextChannelState.clip = newClip;

            // play
            if(!audio_engine_playRaw( apu->audio, newState->channelId, &nextChannelState ))
            {
                // Add generic error if not handled
                if(c->errorCode == SDKErr_OK)
                c->errorCode = SDKErr_Fail;
            }
            }
        }

        if(newState->fieldBits & AudioEngineChannelWriteStateDataBits_addSeq)  // seq
        {
            for(int j=0;j<newState->clipSeqCnt;j++)
            audio_engine_queue_seq( apu->audio, newState->channelId, newState->clipSeq[j] );          
        }

    } // for

    // Read off stream clips
    if(cmd->streamWriteBufferSz > 0)
    {
        // Ensure fits cmd
        if(cmd->header.sz == cmd->streamWriteBufferSz + sizeof(APUCMD_WriteAudioEngineState))
        {
            // get clip
            AudioClip_t* clip = audio_engine_getClipById( apu->audio, cmd->streamWriteBufferId);
            if(clip)
            {
            // write into clip, assume block matches exact
            if(clip->ramSourceData && clip->sourceSz == cmd->streamWriteBufferSz)
            {
                memcpy(clip->ramSourceData, cmd->streamData, cmd->streamWriteBufferSz );
            }
            }
        }
    }

    return 1;
}


uint16_t apu_calc_file_crc( struct apu_t* apu, const char* filename )
{
#ifdef PICOCOM_NATIVE_SIM  
  return 0; // TODO
#else
  // Open file for reading
  FRESULT fr;
  FIL fil;
  fr = f_open(&fil, filename, EFileMode_Read);
  if( fr != FR_OK )
  {    
    return 0;    
  }

  // Query size
  int size = f_size(&fil);

  // alloc temp
  uint8_t* buff = picocom_malloc( size) ;
  if(!buff)
    return 0;

  // read off data
  UINT br;
  fr = f_read(&fil, buff, size, &br);
  if(fr != FR_OK)
  {
    picocom_free(buff);
    return 0;
  }

  uint16_t crc =  picocom_crc16( (const char*)buff, size);

  f_close(&fil);

  return crc;
#endif  
}


int apu_handle_open_file_cmd( struct apu_t* apu, struct Cmd_SD_OpenFile* cmd , struct Res_SD_OpenFile* res, uint32_t asyncIoReqId )
{
  BUS_INIT_CMD_PTR(res, cmd->header.cmd);      

#ifdef PICOCOM_NATIVE_SIM  

  // force termination
  cmd->filename[sizeof(cmd->filename)-1] = 0;

  // Check fp cache
  uint32_t errorCodeOut = 0;
  struct apuFPcache_t* fpCache = apu_open_file(apu, cmd->filename, cmd->mode, &errorCodeOut); // do fp open and alloc cache ( files of same name will not be de-duplicated )

  if(!fpCache)
  {          
#ifdef TRACE_FILEIO
  printf("[apu] open[%d] failed f: '%s'\n", asyncIoReqId, cmd->filename);
#endif

    res->result = errorCodeOut;           
    return 0;
  }

#ifdef TRACE_FILEIO
  printf("[apu] open[%d] '%s', m: %d, id: %llu, sz: %d\n", asyncIoReqId, cmd->filename, cmd->mode, res->fileHandle, fpCache->size);
#endif
      
  res->result = 0;       
  res->fileHandle = fpCache->fileHandleId;
  res->size = fpCache->size;

#else
  // force termination
  cmd->filename[sizeof(cmd->filename)-1] = 0;

  // Check fp cache
  uint32_t errorCodeOut = 0;
  struct apuFPcache_t* fpCache = apu_open_file(apu, cmd->filename, cmd->mode, &errorCodeOut); // do fp open and alloc cache ( files of same name will not be de-duplicated )

  if(!fpCache)
  {          
#ifdef TRACE_FILEIO
  printf("[apu] open[%d] failed f: '%s'\n", asyncIoReqId, cmd->filename);
#endif

    res->result = errorCodeOut;           
    return 0;
  }

#ifdef TRACE_FILEIO
  printf("[apu] open[%d] '%s', m: %d, id: %ld, sz: %d\n", asyncIoReqId, cmd->filename, cmd->mode, res->fileHandle, fpCache->size);
#endif  
  
  res->result = 0;       
  res->fileHandle = fpCache->fileHandleId;
  res->size = fpCache->size;  

#endif // NATIVE_SIM      
    return 1;
}


int apu_handle_close_file_cmd( struct apu_t* apu, struct Cmd_SD_CloseFile* cmd , struct Res_SD_CloseFile* res, uint32_t asyncIoReqId )
{
  BUS_INIT_CMD_PTR(res, cmd->header.cmd);      

#ifdef PICOCOM_NATIVE_SIM   

    // get file
    struct apuFPcache_t* fpCache = apu_find_fp_by_handleId(apu, cmd->fileHandle); // get by handle
    if(!fpCache)
    {
    res->result = EFileIOError_InvalidFileHandle;             
    return 0;
    }

    if(fpCache->isDir)
    {
#ifdef TRACE_FILEIO
    printf("[apu] close[%d] dir f: '%s', id: %llu\n", asyncIoReqId, fpCache->filename, fpCache->fileHandleId );
#endif

    if(fpCache->dir)
        closedir(fpCache->dir);
    fpCache->dir = 0;
    }
    else
    {
#ifdef TRACE_FILEIO
    uint16_t finalCrc =  apu_calc_file_crc( apu, fpCache->filename );
    printf("[apu] close[%d] file f: '%s', id: %ld, crc_on_disk: %x\n", asyncIoReqId, fpCache->filename, fpCache->fileHandleId, finalCrc );
#endif

    if(fpCache->fil)
        fclose(fpCache->fil);
    fpCache->fil = 0;
    }
    fpCache->isValid = false;

    BUS_INIT_CMD_PTR(res, cmd->header.cmd);      
    res->result = 0;

#else

    // get file
    struct apuFPcache_t* fpCache = apu_find_fp_by_handleId(apu, cmd->fileHandle); // get by handle
    if(!fpCache)
    {
      res->result = EFileIOError_InvalidFileHandle;             
      return 0;
    }

    if(fpCache->isDir)
    {
#ifdef TRACE_FILEIO
      printf("[apu] close[%d] dir f: '%s', id: %ld\n", asyncIoReqId, fpCache->filename, fpCache->fileHandleId );
#endif

      f_closedir(&fpCache->dir);
    }
    else
    {
      f_close(&fpCache->fil);
    
#ifdef TRACE_FILEIO
    uint16_t finalCrc =  apu_calc_file_crc( apu, fpCache->filename );
    printf("[apu] close[%d] file f: '%s', id: %ld, crc_on_disk: %x\n", asyncIoReqId, fpCache->filename, fpCache->fileHandleId, finalCrc );
#endif
    }

    fpCache->isValid = false;

    BUS_INIT_CMD_PTR(res, cmd->header.cmd);        
    res->result = 0;

#endif  // PICOCOM_NATIVE_SIM   

    return 1;
}


int apu_handle_read_file_cmd( struct apu_t* apu, struct Cmd_SD_ReadFile* cmd , struct Res_SD_ReadFile* res, uint32_t asyncIoReqId )
{
  BUS_INIT_CMD_PTR(res, cmd->header.cmd);
#ifdef PICOCOM_NATIVE_SIM   

  // get file
  struct apuFPcache_t* fpCache = apu_find_fp_by_handleId(apu, cmd->fileHandle); // get by handle
  if(!fpCache || fpCache->isDir || !fpCache->fil)
  {
    res->result = EFileIOError_InvalidFileHandle;           
    return 0;
  }

  if(cmd->size > sizeof(res->buffer))
  {
    res->result = EFileIOError_ReadOverflow;           
    return 0;
  }

  int fr = fseek(fpCache->fil, cmd->offset, SEEK_SET);      
  if(fr < 0)
  {
    res->result = EFileIOError_General;    
    return 0;
  }

  fr = fread(res->buffer, 1, cmd->size, fpCache->fil);
  if(fr < 0)
  {
    res->result = EFileIOError_General;           
    return 0;
  }

#ifdef TRACE_FILEIO
      printf("[apu] read[%d] f: '%s', id: %llu, cmdSz: %d, cmdOffs: %d, br: %d\n", asyncIoReqId, fpCache->filename, fpCache->fileHandleId, cmd->size, cmd->offset, fr );
#endif

    res->result = 0;
    res->size = fr;
    res->isEof = fr == 0; // TODO: this wont work
#else

  // get file
  struct apuFPcache_t* fpCache = apu_find_fp_by_handleId(apu, cmd->fileHandle); // get by handle
  if(!fpCache || fpCache->isDir)
  {
    res->result = EFileIOError_InvalidFileHandle;           
    return 0;
  }

  if(cmd->size > sizeof(res->buffer))
  {
    res->result = EFileIOError_ReadOverflow;           
    return 0;
  }

  FRESULT fr;
  // seek to offset
  fr = f_lseek(&fpCache->fil, cmd->offset);
  if(fr != FR_OK)
  {
    res->result = sdToStorageError(fr);           
    return 0;
  }

  UINT br;
  fr = f_read(&fpCache->fil, res->buffer, cmd->size, &br);
  if(fr != FR_OK)
  {
    res->result = sdToStorageError(fr);           
    return 0;
  }

#ifdef TRACE_FILEIO
      printf("[apu] read[%d] f: '%s', id: %ld, cmdSz: %d, cmdOffs: %d, br: %d\n", asyncIoReqId, fpCache->filename, fpCache->fileHandleId, cmd->size, cmd->offset, br );
#endif

  res->result = 0;
  res->size = br;
  res->isEof = br == 0;

#endif // PICOCOM_NATIVE_SIM

  return 1;
}


int apu_handle_write_file_cmd( struct apu_t* apu, struct Cmd_SD_WriteFile* cmd , struct Res_SD_WriteFile* res, uint32_t asyncIoReqId )
{
  BUS_INIT_CMD_PTR(res, cmd->header.cmd);
  res->asyncIoReqId = asyncIoReqId;
#ifdef PICOCOM_NATIVE_SIM   

  // get file
  struct apuFPcache_t* fpCache = apu_find_fp_by_handleId(apu, cmd->fileHandle); // get by handle
  if(!fpCache || fpCache->isDir || !fpCache->fil)
  {
    res->result = EFileIOError_InvalidFileHandle;           
    return 0;
  }

  int fr = fseek(fpCache->fil, cmd->offset, SEEK_SET);      
  if(fr < 0)
  {
    res->result = EFileIOError_General;    
    return 0;
  }
  
  fr = fwrite(cmd->buffer, 1, cmd->size, fpCache->fil);
  if(fr < 0)
  {
    res->result = EFileIOError_General;         
    return 0;
  }

#ifdef TRACE_FILEIO
    printf("[apu] write[%d] f: '%s', id: %llu, cmdSz: %d, cmdOffs: %d, br: %d\n", asyncIoReqId, fpCache->filename, fpCache->fileHandleId, cmd->size, cmd->offset, fr );
#endif      

    res->result = 0;
    res->size = fr;
#else

  // get file
  struct apuFPcache_t* fpCache = apu_find_fp_by_handleId(apu, cmd->fileHandle); // get by handle
  if(!fpCache || fpCache->isDir)
  {
    res->result = EFileIOError_InvalidFileHandle;           
    return 0;
  }

  FRESULT fr;
  // seek to offset
  fr = f_lseek(&fpCache->fil, cmd->offset);
  if(fr != FR_OK)
  {
    res->result = sdToStorageError(fr);       
    return 0;
  }

  UINT br;
  fr = f_write(&fpCache->fil, cmd->buffer, cmd->size, &br);
  if(fr != FR_OK)
  {
    res->result = sdToStorageError(fr);           
    return 0;
  }

  // Check disk space
  if( br < cmd->size)
  {
    DWORD nclst = 0;
    FATFS *fs = &apu->fs;
    fr = f_getfree ("0:", &nclst, &fs );	/* Get number of free clusters on the drive */
    printf("[apu] debug free clusters: %d\n", (int)nclst);

    res->result = EFileIOError_WriteDiskFull;    
    return 0;        
  }

#ifdef TRACE_FILEIO
  printf("[apu] write[%d] f: '%s', id: %ld, cmdSz: %d, cmdOffs: %d, br: %d\n", asyncIoReqId, fpCache->filename, fpCache->fileHandleId, cmd->size, cmd->offset, br );
#endif      

  res->result = 0;
  res->size = br;
  
#endif // PICOCOM_NATIVE_SIM

  return 1;
}


int apu_handle_stat_file_cmd( struct apu_t* apu, struct Cmd_SD_FileStat* cmd , struct Res_SD_FileStat* res, uint32_t asyncIoReqId )
{
  BUS_INIT_CMD_PTR(res, cmd->header.cmd);

#ifdef PICOCOM_NATIVE_SIM

    // force termination
    cmd->filename[sizeof(cmd->filename)-1] = 0;

    const char* tmpFilename = append_tmp_path_to_mount_dir(tmpMouthPathA, cmd->filename);

    struct stat statInfo;
    int status = stat(tmpFilename, &statInfo);
    if (status == -1)
    {
      res->result = EFileIOError_General;             
      return 0;
    }

#ifdef TRACE_FILEIO
  printf("[apu] stat[%d] f: '%s', st_size: %d\n", asyncIoReqId, cmd->filename, statInfo.st_size );
#endif      

    if ( statInfo.st_mode & S_IFREG )
    {        
      res->size = statInfo.st_size;
      res->fileType = EFileType_File;
    }
    else if ( statInfo.st_mode & S_IFDIR )
    {        
      res->fileType = EFileType_Dir;
    }
    
    res->result = 0; 
    
#else

  // force termination
  cmd->filename[sizeof(cmd->filename)-1] = 0;

  FRESULT fr;
  FILINFO fno;
  fr = f_stat(cmd->filename, &fno);
  if(fr != FR_OK)
  {
        
#ifdef TRACE_FILEIO
  printf("[apu] stat[%d] f: '%s', no file\n", asyncIoReqId, cmd->filename );
#endif      

    res->result = sdToStorageError(fr);           
    return 0;
  }

  if (fno.fattrib & AM_DIR)  // Is dir
  {                   
    res->fileType = EFileType_Dir;
  } 
  else // Is file
  {                              
    res->size = fno.fsize;
    res->attrib = fno.fattrib;
    res->date = fno.fdate;
    res->time = fno.ftime;
    res->fileType = EFileType_File;
  }
        
#ifdef TRACE_FILEIO
  printf("[apu] stat[%d] f: '%s', st_size: %d\n", asyncIoReqId, cmd->filename, res->size  );
#endif      

  res->result = 0;

#endif // PICOCOM_NATIVE_SIM

  return 1;
}


//
//
static void app_handler_realtime(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    struct apu_t* apu = (struct apu_t*)bus->userData;

    switch(frame->cmd)
    { 
    default:
    {
        bus_rx_push_defer_cmd(bus, frame);            
        break;
    }    
    }
}


static void app_handler_main(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
  struct apu_t* apu = (struct apu_t*)bus->userData;
  assert(apu);
  assert(apu->audio);

  uint16_t actualCrc = bus_calc_msg_crc(frame);
  if( frame->crc && frame->crc != actualCrc )
  {
    printf("crc fail\n");
  }
    
  switch(frame->cmd)
  {  
    // Device 
    case EBusCmd_APU_GetStatus:
    {    
      struct Cmd_APU_GetStatus* cmd = (struct Cmd_APU_GetStatus*)frame;
      if(cmd->forceMediaCheck)
      {
        autoMountSDCard(apu);
      }

      // Return status      
      static Res_APU_GetStatus res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_GetStatus);        
      update_status_cmd(apu, &res, cmd->clearHIDCounters);            

      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);  
      break;
    }       
    case EBusCmd_APU_Reset:
    {
      apu->resetCount++;

      // reset engine
      audio_engine_reset(apu->audio);

      // clear cache
      apu_clear_fp_cache(apu);

      // Return status
      static Res_APU_GetStatus res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_Reset);              

      update_status_cmd(apu, &res, true);       
           
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);  
      break;
    }

    // Audio
    case EBusCmd_APU_SND_CreateClipMem:
    {
      APUCMD_CreateClipMem* cmd = (APUCMD_CreateClipMem*)frame;

      // unload current clip id
      audio_engine_free_clip(apu->audio, cmd->clipId);

      int resCode;
      if(cmd->isFlash)
        resCode = audio_engine_create_clip_flash(apu->audio, cmd->clipId, (enum EAudioDataFormat)cmd->format, cmd->sourceChannels, cmd->flashBaseOffset, cmd->size);
      else
        resCode = audio_engine_create_clip_mem(apu->audio, cmd->clipId, (enum EAudioDataFormat)cmd->format, cmd->sourceChannels, cmd->flashBaseOffset, cmd->size);

      static Res_int32 res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SND_CreateClipMem);        
      res.result = resCode;       
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);  
      break;
    }
    case EBusCmd_APU_SND_LoadClipMemBlock:
    {
      APUCMD_ClipDataBlock* cmd = (APUCMD_ClipDataBlock*)frame;

      // write clip data
      int resCode = audio_engine_write_clip_block(apu->audio, cmd->clipId, cmd->offset, cmd->data, cmd->sz, cmd->isLast);

      static Res_int32 res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SND_LoadClipMemBlock);        
      res.result = resCode;       
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);          
      break;
    }
    case EBusCmd_APU_SND_ReadAudioEngineState:
    {
      static APUCMD_ReadAudioEngineState readAudioState;        

      // Get state
      update_audio_state_cmd( apu, &readAudioState.audioState );   

      // Return to app
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, frame, &readAudioState.header); 
      break;
    }         
    case EBusCmd_APU_SND_WriteAudioEngineState:
    {
      struct APUCMD_WriteAudioEngineState* cmd = (struct APUCMD_WriteAudioEngineState*)frame;

      // Write audio state
      apu_write_audio_cmd( apu, cmd );
      
      // Returns status ( legacy )
      static Res_APU_GetStatus res = {};  
      ZERO_MEM(res);    
      BUS_INIT_CMD(res, EBusCmd_APU_SND_WriteAudioEngineState);        
      update_status_cmd(apu, &res, true);            

      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);        
      break;
    }

    case EBusCmd_APU_MultiAudioStatusSD:
    {
      struct Cmd_MultiAudioStatusSD* multiCmd = (struct Cmd_MultiAudioStatusSD*)frame;
      static Res_MultiAudioStatusSD multiRes = {};
      ZERO_MEM(multiRes);
      BUS_INIT_CMD(multiRes, EBusCmd_APU_MultiAudioStatusSD);     
      
      // Get status
      if( multiCmd->getStatus.header.cmd && multiCmd->getStatus.header.sz )
      {        
        BUS_INIT_CMD(multiRes.getStatus, EBusCmd_APU_GetStatus);        
        update_status_cmd(apu, &multiRes.getStatus, multiCmd->getStatus.clearHIDCounters);            
      }   
      
      // Audio state
      if( multiCmd->audioState.header.cmd && multiCmd->audioState.header.sz )
      {                
        apu_write_audio_cmd(apu, &multiCmd->audioState );            
      }

      // IO state
      multiRes.ioCmd = multiCmd->ioCmd;
      if( multiRes.ioCmd )
      {        
        switch( multiCmd->ioCmd)
        {
        case EBusCmd_APU_SD_OpenFile:
        case EBusCmd_APU_SD_OpenFileAsync:
            apu_handle_open_file_cmd(apu, &multiCmd->openCmd, &multiRes.openCmd, multiCmd->asyncIoReqId );
            break;
        case EBusCmd_APU_SD_CloseFile:
        case EBusCmd_APU_SD_CloseFileAsync:
            apu_handle_close_file_cmd(apu, &multiCmd->closeCmd, &multiRes.closeCmd, multiCmd->asyncIoReqId );
            break;          
        case EBusCmd_APU_SD_ReadFile:
        case EBusCmd_APU_SD_ReadFileAsync:
            apu_handle_read_file_cmd(apu, &multiCmd->readCmd, &multiRes.readCmd, multiCmd->asyncIoReqId );
            break;                    
        case EBusCmd_APU_SD_WriteFile:
        case EBusCmd_APU_SD_WriteFileAsync:
            apu_handle_write_file_cmd(apu, &multiCmd->writeCmd, &multiRes.writeCmd, multiCmd->asyncIoReqId );
            break;          
        case EBusCmd_APU_SD_FileStat:
        case EBusCmd_APU_SD_FileStatAsync:
            apu_handle_stat_file_cmd(apu, &multiCmd->statCmd, &multiRes.statCmd, multiCmd->asyncIoReqId );
            break;                                 
        default:
          printf("[apu] invalid multiCmd->ioCmd: %d\n", multiCmd->ioCmd);            
        }
      }

      multiRes.asyncIoReqId = multiCmd->asyncIoReqId;

      if( multiRes.ioCmd )
      {
        // sdcard sleep        
        multiRes.header.crc = bus_calc_msg_crc(&multiRes.header);
        apu->lastAsyncIOTxSeqNum = apu->app_alnk_tx->lastSeqNum;
#ifdef TRACE_FILEIO           
        printf("apu->lastAsyncIOTxSeqNum: %d, crc: %x\n", apu->lastAsyncIOTxSeqNum, multiRes.header.crc);
#endif        
      }

      // Return status   
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &multiRes.header, &multiRes.header);           

      break;
    }

    // HID
    case EBusCmd_APU_GetHIDState:
    {    
      struct Cmd_APU_HIDState* cmd = (struct Cmd_APU_HIDState*)frame;

      // Return status      
      static Res_APU_HIDState res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_GetHIDState);        
      
      // HID state    
      struct HIDCMD_getState* hidState = hid_driver_get_state();
      if(!hidState) 
      {
        res.result = SDKErr_Fail;       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }
        
      res.hidState = *hidState;

      // reset buffer counters
      if(cmd->clearHIDCounters)
      {
        hidState->mouseCnt = 0;
        hidState->keyboardCnt = 0;
        hidState->gamepadCnt = 0;
      }

      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);  
      break;
    }      
    // Storage - sdcard / fatfs
    case EBusCmd_APU_SD_OpenFile:
    case EBusCmd_APU_SD_OpenFileAsync:
    {
      struct Cmd_SD_OpenFile* cmd = (struct Cmd_SD_OpenFile*)frame;
      static struct Res_SD_OpenFile res = {};
      ZERO_MEM(res);

      apu_handle_open_file_cmd( apu, cmd, &res, 0 );

      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
      return;
    }  
    case EBusCmd_APU_SD_CloseFile:
    case EBusCmd_APU_SD_CloseFileAsync:
    {
      struct Cmd_SD_CloseFile* cmd = (struct Cmd_SD_CloseFile*)frame;
      static struct Res_SD_CloseFile res = {};
      ZERO_MEM(res);

      apu_handle_close_file_cmd( apu, cmd, &res, 0 );

      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);
      break;      
    }     
    case EBusCmd_APU_SD_ReadFile:
    case EBusCmd_APU_SD_ReadFileAsync:
    {
      struct Cmd_SD_ReadFile* cmd = (struct Cmd_SD_ReadFile*)frame;
      static struct Res_SD_ReadFile res = {};
      ZERO_MEM(res);

      apu_handle_read_file_cmd( apu, cmd, &res, 0 );

      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);
      break;      
    }     
    case EBusCmd_APU_SD_WriteFile:
    case EBusCmd_APU_SD_WriteFileAsync:
    {      
      struct Cmd_SD_WriteFile* cmd = (struct Cmd_SD_WriteFile*)frame;
      static struct Res_SD_WriteFile res = {};
      ZERO_MEM(res);

      apu_handle_write_file_cmd( apu, cmd, &res, 0 );

      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);
      break;      
    }

    case EBusCmd_APU_SD_FileStat:
    case EBusCmd_APU_SD_FileStatAsync:
    {
      struct Cmd_SD_FileStat* cmd = (struct Cmd_SD_FileStat*)frame;
      static struct Res_SD_FileStat res = {};
      ZERO_MEM(res);
      
      apu_handle_stat_file_cmd( apu, cmd, &res, 0 );

      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);    
      break;
    }     

#ifdef PICOCOM_NATIVE_SIM        
    case EBusCmd_APU_SD_OpenDir:
    {    
      struct Cmd_SD_OpenFile* cmd = (struct Cmd_SD_OpenFile*)frame;

      static struct Res_SD_OpenFile res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SD_OpenDir);        

      // force termination
      cmd->filename[sizeof(cmd->filename)-1] = 0;

      // Check fp cache
      uint32_t errorCodeOut = 0;
      struct apuFPcache_t* fpCache = apu_open_dir(apu, cmd->filename, &errorCodeOut); // do fp open and alloc cache ( files of same name will not be de-duplicated )

      if(!fpCache)
      {          
        res.result = errorCodeOut;       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      res.result = 0;       
      res.fileHandle = fpCache->fileHandleId;
      res.size = fpCache->size;
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
      return;
        
    }
    case EBusCmd_APU_SD_ListDirNext:
    {
      struct Cmd_SD_ListDirNext* cmd = (struct Cmd_SD_ListDirNext*)frame;
      
      static struct Res_SD_ListDirNext res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SD_ListDirNext);        

      // get file
      struct apuFPcache_t* fpCache = apu_find_fp_by_handleId(apu, cmd->fileHandle); // get by handle
      if(!fpCache || !fpCache->isDir || !fpCache->dir)
      {
        res.result = EFileIOError_InvalidFileHandle;       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      struct dirent *de = readdir(fpCache->dir);
      if(!de)
      {
        res.isEof = true;
        res.result = 0; 
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;  
      }

      const char* tmpFilename = append_tmp_to_dir(tmpMouthPathA, fpCache, de->d_name);

      struct stat statInfo;
      int status = stat(tmpFilename, &statInfo);
      if (status == -1)
      {
        res.result = EFileIOError_General;       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }
      
      memset(res.filename,0, sizeof(res.filename));
      if ( statInfo.st_mode & S_IFREG )
      {
        strncpy(res.filename, de->d_name, sizeof(res.filename));
        res.size = statInfo.st_size;
        res.fileType = EFileType_File;
      }
      else if ( statInfo.st_mode & S_IFDIR )
      {
        strncpy(res.filename, de->d_name, sizeof(res.filename));        
        res.fileType = EFileType_Dir;
      }
      
      res.isEof = false;
      res.result = 0; 
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);
      return;
      
    }    
    case EBusCmd_APU_SD_MoveFile:
    {
      struct Cmd_SD_MoveFile* cmd = (struct Cmd_SD_MoveFile*)frame;

      static struct Res_SD_MoveFile res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SD_MoveFile);        

      // force termination
      cmd->from[sizeof(cmd->from)-1] = 0;
      cmd->to[sizeof(cmd->to)-1] = 0;

      const char* fromFilename = append_tmp_path_to_mount_dir(tmpMouthPathA, cmd->from);
      const char* toFilename = append_tmp_path_to_mount_dir(tmpMouthPathB, cmd->to);

      int fr;

      // force
      if(cmd->overwrite)
      {        
        sanity_check_path_prefix(toFilename);
      
        struct stat statInfo;
        int status = stat(toFilename, &statInfo);
        if (status == 0)
        {          
          // remove old
          fr = unlink(toFilename);
          if(fr != 0)
          {
            res.result = EFileIOError_General;       
            bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
            return;
          }          
        }
      }
      
      // seek to offset
      sanity_check_path_prefix(fromFilename);
      sanity_check_path_prefix(toFilename);

      struct stat statInfo;
      int status = stat(toFilename, &statInfo);
      if (status == 0)
      {     
          res.result = EFileIOError_General;       
          bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
          return;    
      }

      fr = rename(fromFilename, toFilename);

#ifdef TRACE_FILEIO
      printf("[apu] rename from: '%s', to: %s, fr: %d\n", fromFilename, toFilename, fr );
#endif     

      if(fr < 0)
      {
        res.result = EFileIOError_General;
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      res.result = 0;             
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
      return;
    }
    case EBusCmd_APU_SD_UnlinkFile:
    {
      struct Cmd_SD_UnlinkFile* cmd = (struct Cmd_SD_UnlinkFile*)frame;

      static struct Res_SD_UnlinkFile res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SD_UnlinkFile);        

      // force termination
      cmd->filename[sizeof(cmd->filename)-1] = 0;

      const char* tmpFilename = append_tmp_path_to_mount_dir(tmpMouthPathA, cmd->filename);

      int fr;      
      sanity_check_path_prefix(tmpFilename);
      fr = unlink(tmpFilename);
      if(fr != 0)
      {
        res.result = EFileIOError_General;       
        sanity_check_path_prefix(tmpFilename);
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      res.result = 0;             
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
      return;      
    } 

#else

    // Storage - sdcard / fatfs        
    case EBusCmd_APU_SD_OpenDir:
    {
      struct Cmd_SD_OpenFile* cmd = (struct Cmd_SD_OpenFile*)frame;

      static struct Res_SD_OpenFile res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SD_OpenDir);        

      // force termination
      cmd->filename[sizeof(cmd->filename)-1] = 0;

      // Check fp cache
      uint32_t errorCodeOut = 0;
      struct apuFPcache_t* fpCache = apu_open_dir(apu, cmd->filename, &errorCodeOut); // do fp open and alloc cache ( files of same name will not be de-duplicated )

      if(!fpCache)
      {          
        res.result = errorCodeOut;       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      res.result = 0;       
      res.fileHandle = fpCache->fileHandleId;
      res.size = fpCache->size;
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
      return;
    }
    case EBusCmd_APU_SD_ListDirNext:
    {
      struct Cmd_SD_ListDirNext* cmd = (struct Cmd_SD_ListDirNext*)frame;
      
      static struct Res_SD_ListDirNext res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SD_ListDirNext);        

      // get file
      struct apuFPcache_t* fpCache = apu_find_fp_by_handleId(apu, cmd->fileHandle); // get by handle
      if(!fpCache || !fpCache->isDir)
      {
        res.result = EFileIOError_InvalidFileHandle;       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      FILINFO fno;
      FRESULT fr;
      fr = f_readdir(&fpCache->dir, &fno);  
      if(fr != FR_OK)
      {
        res.result = sdToStorageError(fr);       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      // Check EOF
      if (fno.fname[0] == 0) 
      {
        res.isEof = true;
        res.result = 0; 
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;        
      }

      if (fno.fattrib & AM_DIR)  // Is dir
      {           
        size_t maxSz = MIN(sizeof(fno.fname), sizeof(res.filename));
        strncpy(res.filename, fno.fname, maxSz);        
        res.fileType = EFileType_Dir;
      } 
      else // Is file
      {                      
        strncpy(res.filename, fno.fname, MIN(sizeof(fno.fname), sizeof(res.filename)));
        res.size = fno.fsize;
        res.attrib = fno.fattrib;
        res.date = fno.fdate;
        res.time = fno.ftime;
        res.fileType = EFileType_File;
      }
      
      res.isEof = false;
      res.result = 0; 
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
      return;             
    }     
    case EBusCmd_APU_SD_MoveFile:
    {
      struct Cmd_SD_MoveFile* cmd = (struct Cmd_SD_MoveFile*)frame;

      static struct Res_SD_MoveFile res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SD_MoveFile);        

      // force termination
      cmd->from[sizeof(cmd->from)-1] = 0;
      cmd->to[sizeof(cmd->to)-1] = 0;

      FRESULT fr;

      // force
      if(cmd->overwrite)
      {
        FILINFO fno;
        fr = f_stat(cmd->to, &fno);
        if(fr==FR_OK)
        {
          // remove old
          fr = f_unlink(cmd->to);
          if(fr != FR_OK)
          {
            res.result = sdToStorageError(fr);       
            bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
            return;
          }          
        }
      }
      
      // seek to offset
      fr = f_rename(cmd->from, cmd->to);
      if(fr != FR_OK)
      {
        res.result = sdToStorageError(fr);       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      res.result = 0;             
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
      return;
    }
    case EBusCmd_APU_SD_UnlinkFile:
    {
      struct Cmd_SD_UnlinkFile* cmd = (struct Cmd_SD_UnlinkFile*)frame;

      static struct Res_SD_UnlinkFile res = {};
      ZERO_MEM(res);
      BUS_INIT_CMD(res, EBusCmd_APU_SD_UnlinkFile);        

      // force termination
      cmd->filename[sizeof(cmd->filename)-1] = 0;

      FRESULT fr;
      // seek to offset
      fr = f_unlink(cmd->filename);
      if(fr != FR_OK)
      {
        res.result = sdToStorageError(fr);       
        bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
        return;
      }

      res.result = 0;             
      bus_tx_rpc_set_return_main(apu->app_alnk_tx, &res.header, &res.header);     
      return;
    }      
    #endif // Fs type
  } // switch
}


int apu_deinit(struct apu_t* apu)
{
    if(apu->audio)
    {
      audio_engine_deinit(apu->audio);
      apu->audio = 0;
    }
    return SDKErr_OK;
}


void autoMountSDCard(struct apu_t* apu)
{  
  bool sdDetected = false;
#ifdef PICOCOM_SDL
  sdDetected = storage_has_mount();  
#else  
  // Detect sdcard in slot  
  sd_card_t *p_sd = sd_get_by_num(0);
  if(p_sd)
    sdDetected = sd_card_detect(p_sd);
  else
    sdDetected = false;  
#endif

    // init sd driver ( needed for card detect)
#ifdef PICOCOM_PICO   
    if( !apu->sdDriverInited )
    {
      apu->sdDriverInited = sd_init_driver();
      if(!apu->sdDriverInited)
      {
        printf("SD init failed \n"); // Exit and wailt for re-insert state change        
      }
    }
#else
    apu->sdDriverInited = true;
#endif

  // Handle disk io error
  if( apu->sdCardPendingRestartOnDiskErr )
  {
    printf("disk io, forcing eject\n");
    apu->sdCardPendingRestartOnDiskErr = false;
    sdDetected = false;
  }

#ifndef PICOCOM_NATIVE_SIM    
  // Handle card insert/eject changes
  if( sdDetected && !apu->sdCardDetected ) // Insert
  {    
    // ensure not mounted
    if( apu->sdCardMounted )
    {
      // clear fp cache
      apu_clear_fp_cache(apu);

      // unmount
      f_mount(NULL, "0:", 0); 
      
      apu->sdCardMounted = false;
    }

    if( apu->sdDriverInited )
    {
      // Allow some time for insertion
      picocom_sleep_ms(100);  

      // try mount
      FRESULT fr;      
      fr = f_mount(&apu->fs, "0:", 1);         
      if (fr != FR_OK) 
      {
        printf("SD mount failed\n");      // Exit and wailt for re-insert state change
      }
      else
      {
        apu->sdCardMounted = true;    

        DWORD nclst = 0;
        FATFS *fs = &apu->fs;
        fr = f_getfree ("0:", &nclst, &fs );	/* Get number of free clusters on the drive */
        printf("[apu] debug free clusters: %d\n", (int)nclst);

      }
    }
  } 
  else if( !sdDetected && apu->sdCardDetected ) // Eject
  {
    // ensure not mounted
    if( apu->sdCardMounted )
    {
      // clear fp cache
      apu_clear_fp_cache(apu);

      // unmount
      f_mount(NULL, "0:", 0); 
      
      apu->sdCardMounted = false;
    }

    // release driver
#ifdef PICOCOM_PICO       
    if(apu->sdDriverInited)
    {
      sd_deinit_driver();
      apu->sdDriverInited = false;
    }
#endif
  }

  apu->sdCardDetected = sdDetected;
#endif  
}


void apu_clear_fp_cache(struct apu_t* apu)
{
  for(int i=0;i<NUM_ELEMS(apu->fpCache);i++)
  {
    struct apuFPcache_t* fp = &apu->fpCache[i];
#ifndef PICOCOM_NATIVE_SIM        
    if(fp->isValid)
    {
      if(fp->isDir)
      {
        f_closedir(&fp->dir);
      }
      else
      {
        f_close(&fp->fil);
      }
    }
#endif
#ifdef PICOCOM_NATIVE_SIM   
    if(fp->isValid)
    {
      if(fp->isDir)
      {
        if(fp->dir)
          closedir(fp->dir);
        fp->dir = 0;
      }
      else
      {
        if(fp->fil)
          fclose(fp->fil);
        fp->fil = 0;
      }
    }
#endif
    fp->isValid = false;
  }
}


struct apuFPcache_t* apu_find_fp_by_filename(struct apu_t* apu, const char* filename)
{
  for(int i=0;i<NUM_ELEMS(apu->fpCache);i++)
  {
    struct apuFPcache_t* fp = &apu->fpCache[i];
    if(!fp->isValid)
      continue;
    
    if(strncmp(filename, fp->filename, sizeof(fp->filename)) != 0)
      continue;
    
    return fp;
  }
  return 0;
}


struct apuFPcache_t* apu_find_fp_by_handleId(struct apu_t* apu, uint64_t handleId)
{
  for(int i=0;i<NUM_ELEMS(apu->fpCache);i++)
  {
    struct apuFPcache_t* fp = &apu->fpCache[i];
    if(!fp->isValid)
      continue;
    
    if(fp->fileHandleId != handleId)
      continue;
    
    return fp;
  }
  return 0;
}

#ifdef PICOCOM_NATIVE_SIM    

const char* append_tmp_path_to_mount_dir(char* tmpMouthPath, const char* filename)
{
  tmpMouthPath[0] = 0;

  const char* mountDir = storage_get_dir();
  strncat(tmpMouthPath, mountDir, sizeof(tmpMouthPathA)-1);  
  strncat(tmpMouthPath, "/", sizeof(tmpMouthPathA)-1);  
  strncat(tmpMouthPath, filename, sizeof(tmpMouthPathA)-1);  

  return tmpMouthPath;
}


const char* append_tmp_to_dir(char* tmpMouthPath, struct apuFPcache_t* handle, const char* filename)
{
  tmpMouthPath[0] = 0;

  strncat(tmpMouthPath, handle->filename, sizeof(tmpMouthPathA)-1);  
  strncat(tmpMouthPath, "/", sizeof(tmpMouthPathA)-1);  
  strncat(tmpMouthPath, filename, sizeof(tmpMouthPathA)-1);  

  return tmpMouthPath;
}


struct apuFPcache_t* apu_open_dir(struct apu_t* apu, const char* filename, uint32_t* errorCodeOut)
{
  if(!errorCodeOut)
    return 0;
  *errorCodeOut = 0;

  struct apuFPcache_t* handle = 0;    
  for(int i=0;i<NUM_ELEMS(apu->fpCache);i++)
  {
    struct apuFPcache_t* fp = &apu->fpCache[i];
    if(fp->isValid)
      continue;
    
    // get ref
    handle = fp;
    handle->fileHandleId = lastFileHandleId++; // init handle to index
    break;
  }

  if(!handle)
  {
    *errorCodeOut = EFileIOError_TooManyFileHandles;
    return 0;
  }

  const char* tmpFilename = append_tmp_path_to_mount_dir(tmpMouthPathA, filename);

  // Open file for reading
  sanity_check_path_prefix(tmpFilename);
  handle->dir = opendir(tmpFilename);
  if(!handle->dir)
  {
    *errorCodeOut = EFileIOError_NoPath;
    return 0;
  }

  // Query size
  handle->size = 0;

  // Mark valid
  handle->isDir = true;
  handle->isValid = true;
  strcpy(handle->filename, tmpFilename);

  return handle;
}


struct apuFPcache_t* apu_open_file(struct apu_t* apu, const char* filename, int mode, uint32_t* errorCodeOut)
{
  if(!errorCodeOut)
    return 0;
  *errorCodeOut = 0;

  struct apuFPcache_t* handle = 0;    
  for(int i=0;i<NUM_ELEMS(apu->fpCache);i++)
  {
    struct apuFPcache_t* fp = &apu->fpCache[i];
    if(fp->isValid)
      continue;
    
    // get ref
    handle = fp;
    handle->fileHandleId = lastFileHandleId++; // init handle to index
    break;
  }

  if(!handle)
  {
    *errorCodeOut = EFileIOError_TooManyFileHandles;
    return 0;
  }

  // Convert file mode
  const char* nativeMode = 0;

  if( mode & EFileMode_Read)
  {
     nativeMode = "r";

  }
  else if( mode & EFileMode_Write)
  {
    nativeMode = "w"; 

    if( mode & EFileMode_OpenAppend)
    {
      nativeMode = "w+"; 
    }
  }
  else
  {
    *errorCodeOut = EFileIOError_General;    
    return 0;
  }

  // Open file for reading  
  const char* tmpFilename = append_tmp_path_to_mount_dir(tmpMouthPathA, filename);

  sanity_check_path_prefix(tmpFilename);
  handle->fil = fopen(tmpFilename, nativeMode);  
  if( !handle->fil )
  {    
    *errorCodeOut = EFileIOError_NoPath;
    return 0; 
  }

  // Query size
  struct stat statInfo;
  int status = stat(tmpFilename, &statInfo);
  if (status == -1)
  {    
    fclose( handle->fil);
    handle->fil  = 0;
    return 0;
  }

  handle->size = statInfo.st_size;

  // Mark valid
  handle->isDir = false;
  handle->isValid = true;
  strcpy(handle->filename, filename);

  return handle;
}


bool is_path_prefix_valid( const char* path )
{
  if(!path)
    return 0;

  int len = strlen(path);
  if(!len)
  {
    return 0;
  }

  if( path[len-1] == '.')
  {
    return 0;
  }

  if( !strstr(path,"data/") )
  {
    return 0;
  }

  return 1;
}


void sanity_check_path_prefix( const char* path )
{
  if(!is_path_prefix_valid(path))
  {
    picocom_panic(SDKErr_Fail, "Invalid path");
  }
}


#else

struct apuFPcache_t* apu_open_file(struct apu_t* apu, const char* filename, int mode, uint32_t* errorCodeOut)
{
  if(!errorCodeOut)
    return 0;
  *errorCodeOut = 0;

  struct apuFPcache_t* handle = 0;  
  for(int i=0;i<NUM_ELEMS(apu->fpCache);i++)
  {
    struct apuFPcache_t* fp = &apu->fpCache[i];
    if(fp->isValid)
      continue;
    
    // get ref
    handle = fp;
    handle->fileHandleId = i; // init handle to index
    break;
  }

  if(!handle)
  {
    *errorCodeOut = EFileIOError_TooManyFileHandles;
    return 0;
  }

  // Open file for reading
  FRESULT fr;
  fr = f_open(&handle->fil, filename, mode);
  *errorCodeOut = sdToStorageError(fr);
  if( fr != FR_OK )
  {    
    return 0;    
  }

  // Query size
  handle->size = f_size(&handle->fil);

  // Mark valid
  handle->isDir = false;
  handle->isValid = true;
  strcpy(handle->filename, filename);

  return handle;
}


struct apuFPcache_t* apu_open_dir(struct apu_t* apu, const char* filename, uint32_t* errorCodeOut)
{
  if(!errorCodeOut)
    return 0;
  *errorCodeOut = 0;

  struct apuFPcache_t* handle = 0;  
  for(int i=0;i<NUM_ELEMS(apu->fpCache);i++)
  {
    struct apuFPcache_t* fp = &apu->fpCache[i];
    if(fp->isValid)
      continue;
    
    // get ref
    handle = fp;
    handle->fileHandleId = i; // init handle to index
    break;
  }

  if(!handle)
  {
    *errorCodeOut = EFileIOError_TooManyFileHandles;
    return 0;
  }

  // Open file for reading
  FRESULT fr;
  fr = f_opendir(&handle->dir, filename);  
  *errorCodeOut = sdToStorageError(fr);
  if( fr != FR_OK )
  {    
    return 0;    
  }

  // Query size
  handle->size = f_size(&handle->fil);

  // Mark valid
  handle->isDir = true;
  handle->isValid = true;

  return handle;
}
#endif

#ifdef __EMSCRIPTEN__
struct apu_t* g_notify_apu = 0;
void notify_sync_complete( int err )
{
  printf("notify_sync_complete %d\n", err);
  g_notify_apu->sdCardDetected = 1;
  g_notify_apu->sdCardMounted = 1;
}
#endif

int apu_init(struct apu_t* apu, struct apuInitOptions_t* options)
{
    apu->audio = audio_engine_init();
    if(!apu->audio)
      return SDKErr_Fail;

    apu->app_alnk_tx = options->app_alnk_tx;
    apu->app_alnk_rx = options->app_alnk_rx;

    apu->app_alnk_rx->userData = apu;
    bus_rx_set_callback(apu->app_alnk_rx, app_handler_realtime, app_handler_main);  
        
    // Init HID driver
    hid_driver_init();

    // Mount wasm assets/pre-write    
#ifdef __EMSCRIPTEN__
    printf("mounting vfs\n");
    g_notify_apu = apu;
    EM_ASM(           
        FS.mkdir('/data');        
        FS.mount(IDBFS, {}, '/data');
        
        FS.syncfs(true, function (err) {
            // Error
            console.log("Sycned:", err);
            Module.cwrap('notify_sync_complete', 'void', ['number'])()            
        });
    );   
#elif PICOCOM_NATIVE_SIM       
  // Force mount
  apu->sdCardDetected = 1;
  apu->sdCardMounted = 1;
#endif

    mutex_init(&apu->bufferWriteLock);

    apu->startupTime = picocom_time_ms_32();

    return SDKErr_OK;
}


#ifdef PICOCOM_NATIVE_SIM    

void apu_update(struct apu_t* apu, struct apuMainLoopOptions_t* options)
{
  mutex_enter_blocking(&apu->bufferWriteLock);  

  bool canServiceBus = true;
  if( options )
  {
    if(options->audioRenderOnly )
      canServiceBus = false;
  }
  
  // process rx queues
  if( canServiceBus )
  {
    bus_rx_update(apu->app_alnk_rx);

    // process tx queues
    bus_tx_update(apu->app_alnk_tx);
  
    hid_driver_update();
  }

  // update audio IO
  uint32_t startT;        
  startT = picocom_time_us_32();
  audio_engine_updateDataStreams(apu->audio);
  //last_audioIOTime = time_us_32()-startT;

  // render audio
  startT = picocom_time_us_32();
  audio_engine_updateAudio(apu->audio);    
  //last_audioRenderTime = time_us_32()-startT;    
  
  mutex_exit(&apu->bufferWriteLock);
}

#else

int apu_main(struct apu_t* apu, struct apuMainLoopOptions_t* options)
{
    // update loop (if options set)
    while(1)
    {
        // SD auto mounter
        if( picocom_time_us_32() - apu->lastAutoMountCheckTime > 100000 ) // 1 second mount gpio probe
        {
          apu->lastAutoMountCheckTime = picocom_time_us_32();
          autoMountSDCard(apu);
        }

        // process rx queues
        bus_rx_update(apu->app_alnk_rx);

        // process tx queues
        bus_tx_update(apu->app_alnk_tx);
        
        hid_driver_update();

        // update audio IO
        uint32_t startT;        
        startT = picocom_time_us_32();
        audio_engine_updateDataStreams(apu->audio);
        //last_audioIOTime = time_us_32()-startT;

        // render audio
        startT = picocom_time_us_32();
        audio_engine_updateAudio(apu->audio);    
        //last_audioRenderTime = time_us_32()-startT;    
        
        // reset timeout
        if(apu->resetCount == 0 && picocom_time_ms_32() - apu->startupTime > 2000)        
        {          
          if( !apu->triggeredErrorSfx )
          {
            apu->triggeredErrorSfx = true;
            apu_play_error_sfx( apu );
          }
        }

#ifdef PICOCOM_SDL                
        picocom_sleep_us(1);
#endif        
    }

    return 0;
}
#endif


void apu_play_error_sfx(struct apu_t* apu)
{
  printf("apu_play_error_sfx\n");
  gfx_init();
  gfx_mount_asset_pack(&asset_builtin_sounds);      
  uint16_t clipId = 1;
  struct AudioInfoAsset* audio0Res = (AudioInfoAsset*)gfx_get_resource_info_of_type(Ebuiltin_sounds_failure_drum_sound, EGfxAssetType_PCM);
  uint8_t*  audio0Buff = audio0Res->data;
  uint32_t audio0Sz = audio0Res->dataSize;

  audio_engine_create_clip_embeded( apu->audio, clipId, EAudioDataFormat_RawPCM, audio0Res->channels, audio0Buff, audio0Sz);

  AudioClip_t* newClip = audio_engine_getClipById( apu->audio, clipId );

  AudioChannel_t channelState = {};
  channelState.clip = newClip;
  channelState.volLeft = channelState.volRight = 1.0f;
  channelState.sampleStep = 1.0f;

  audio_engine_playRaw( apu->audio, 0, &channelState );  // Low level play state
}