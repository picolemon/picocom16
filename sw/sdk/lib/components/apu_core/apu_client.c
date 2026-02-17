#include "apu_client.h"
#include "picocom/input/input.h"
#include "picocom/audio/audio.h"
#include "picocom/storage/storage.h"
#include "stdio.h"


//
//
static void apu_handler_realtime(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{    
    struct ApuClientImpl_t* client = (struct ApuClientImpl_t*)bus->userData;

    static uint16_t lastAsyncIOTxSeqNum = 0;
    if( frame->seqNum != lastAsyncIOTxSeqNum + 1 )
    {
        printf("frame->seqNum %d != lastAsyncIOTxSeqNum %d + 1\n", frame->seqNum, lastAsyncIOTxSeqNum );
    }
    lastAsyncIOTxSeqNum = frame->seqNum;

    switch(frame->cmd)
    {        
        default:
        {
            bus_rx_push_defer_cmd(bus, frame);            
            break;
        }
    }
}


static void apu_handler_main(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{    
    struct ApuClientImpl_t* client = (struct ApuClientImpl_t*)bus->userData;

    switch(frame->cmd)
    {
        case EBusCmd_APU_GetStatus:
        case EBusCmd_APU_SND_WriteAudioEngineState:
        {
            struct Res_APU_GetStatus* cmd = (Res_APU_GetStatus*)frame;

            // write HID to input
            if(cmd->hasHIDState)
            {
                input_post_hid_state(&cmd->HIDState);
            }

            // read audio state feedback
            if(cmd->hasAudioState)
            {
                audio_post_audio_state(&cmd->audioState);                
            }

            client->lastHasStorage = cmd->hasSDDetected && cmd->hasSDMounted;
            
            break;
        }
        case EBusCmd_APU_MultiAudioStatusSD:
        {
            struct Res_MultiAudioStatusSD* multiCmd = (Res_MultiAudioStatusSD*)frame;
            if( multiCmd->getStatus.header.cmd && multiCmd->getStatus.header.sz )
            {
                struct Res_APU_GetStatus* cmd = (Res_APU_GetStatus*)&multiCmd->getStatus;

                // write HID to input
                if(cmd->hasHIDState)
                {
                    input_post_hid_state(&cmd->HIDState);
                }

                // read audio state feedback
                if(cmd->hasAudioState)
                {
                    audio_post_audio_state(&cmd->audioState);                
                }

                client->lastHasStorage = cmd->hasSDDetected && cmd->hasSDMounted;                
            }

            if( multiCmd->ioCmd )
            {

#ifdef INJECT_ASYNC_CRC_FAILURES
                // ISSUE: APU bus has lossy bus, causes failure in async io
                int value = rand() % (1000 + 1);
                if(value > 760)
                    frame->crc ++;
#endif    

                if( frame->crc != bus_calc_msg_crc(frame))
                {
                    printf("apu_handler_main crc fail in io result\n");
                    storage_clear_async_state();
                    storage_get()->asyncCrcFail = true;
                }
                else
                {
                    // handle io completion
                    switch( multiCmd->ioCmd )
                    {
                        case EBusCmd_APU_SD_OpenFileAsync:
                        case EBusCmd_APU_SD_CloseFileAsync:   
                        case EBusCmd_APU_SD_ReadFileAsync:                      
                        case EBusCmd_APU_SD_WriteFileAsync:;                  
                        case EBusCmd_APU_SD_FileStatAsync:
                            storage_handle_async_response( &multiCmd->statCmd.header, multiCmd->asyncIoReqId );
                            break;                                                                       
                    }
                }
            }
            break;
        }
        default:
        {
            bus_rx_push_defer_cmd(bus, frame);            
            break;
        }
    }
}


struct ApuClientImpl_t* apu_client_init(struct ApuClientInitOptions_t* options)
{
    struct ApuClientImpl_t* client = (struct ApuClientImpl_t*)picocom_malloc(sizeof(struct ApuClientImpl_t));
    if(!client)
        return 0;
    memset(client, 0, sizeof(struct ApuClientImpl_t));

    client->apuLink_tx = options->apuLink_tx;
    client->apuLink_rx = options->apuLink_rx;
    client->defaultTimeout = 1000;
    
    // set rx handler
    client->apuLink_rx->userData = client;
    bus_rx_set_callback(client->apuLink_rx, apu_handler_realtime, apu_handler_main);

    client->maxCmdSize = APU_UPLOAD_BUFFER_BLOCK_SZ;

    return client;
}
