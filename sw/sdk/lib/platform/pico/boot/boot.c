#include "boot.h"
#include "picocom/devkit.h"
#include <hardware/flash.h>
#include "lib/components/flash_store/flash_store.h"


// macros
#if PICO_RP2040
  #define RESETVECTOR_OFFSET M0PLUS_VTOR_OFFSET
#elif PICO_RP2350
  #define RESETVECTOR_OFFSET M33_VTOR_OFFSET
#endif



//
//
static inline uint32_t to_little_endian(uint32_t v) 
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return v;
#else
        return (((va_arg) & 0xFF000000) >> 24 
            | ((v) & 0x00FF0000) >> 8 
            |  ((v) & 0x0000FF00) << 8  
            | ((v) & 0x000000FF) << 24);
#endif
}


int uf2_init(UF2DataBlock_t *uf2) 
{
    uf2->magicStart0    = to_little_endian(uf2->magicStart0);
    uf2->magicStart1    = to_little_endian(uf2->magicStart1);
    uf2->flags          = to_little_endian(uf2->flags);
    uf2->targetAddr     = to_little_endian(uf2->targetAddr);
    uf2->payloadSize      = to_little_endian(uf2->payloadSize);
    uf2->blockNo        = to_little_endian(uf2->blockNo);
    uf2->numBlocks      = to_little_endian(uf2->numBlocks);    
    uf2->magicEnd       = to_little_endian(uf2->magicEnd);

    return uf2->magicStart0 == 0x0A324655 // UF2
        && uf2->magicStart1 == 0x9E5D5157;
}

int bootserver_boot()
{
  asm volatile(
      "ldr r0, =%[maincode]\n"
      "ldr r1, =%[resetvec]\n"
      "str r0, [r1]\n"
      "ldmia r0, {r0, r1}\n"
      "msr msp, r0\n"
      "bx r1\n"
      :
      : [maincode] "i"(XIP_BASE + BOOT_IMAGE_SIZE), [resetvec] "i"( PPB_BASE + RESETVECTOR_OFFSET )
      :);

   return SDKErr_OK; 
}


//
//
static void boot_forward_handler_realtime(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    switch(frame->cmd)
    {  
    default:
    {
        bus_rx_push_defer_cmd(bus, frame);            
        break;
    }    
    }
}


static void boot_app_handler_realtime(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    switch(frame->cmd)
    {  
    default:
    {
        bus_rx_push_defer_cmd(bus, frame);            
        break;
    }    
    }
}


static void boot_forward_handler_main(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    struct BootServer_t* server = (struct BootServer_t*)bus->userData;
}



static void boot_app_handler_main(struct BusRx_t* bus, struct Cmd_Header_t* frame)
{
    struct BootServer_t* server = (struct BootServer_t*)bus->userData;

    switch (frame->cmd)
    {
    case EBusCmd_Boot_LockCore:
    {
        server->lockCore = true;
        break;
    }
    case EBusCmd_Boot_GetStatus:
    {
        struct Cmd_Boot_GetStatus* cmd = (struct Cmd_Boot_GetStatus*)cmd;        
        static Cmd_Boot_GetStatus res = {};

        // router    
        if(frame->flags & EBusStatusFlags_User0 && server->forwardBusTx && server->forwardBusRx)
        {
            // Blocking call to target
            int r;
            struct Cmd_Boot_GetStatus statusCmd = {};    
            BUS_INIT_CMD(statusCmd, EBusCmd_Boot_GetStatus);
            r = bus_tx_request_blocking(server->forwardBusTx, server->forwardBusRx, &statusCmd.header, &res.header, sizeof(res), 10000 );    
            if(r == SDKErr_OK)
            {
                bus_tx_rpc_set_return_main(server->appTx, &res.header, &res.header);  
            }
        }
        else
        {
            BUS_INIT_CMD(res, EBusCmd_Boot_GetStatus);        
            res.coreId = server->coreId;
            res.isReady = true;
            res.coreLocked = server->lockCore;            
            bus_tx_rpc_set_return_main(server->appTx, &res.header, &res.header);  
        }
        break;
    }
    case EBusCmd_Boot_BootApplication:
    {
        server->running = false; // Exit service loop
        break;
    }
    case EBusCmd_Boot_EraseFlashBlock:
    {
        struct Cmd_Boot_EraseFlashBlock* cmd = (struct Cmd_Boot_EraseFlashBlock*)frame;
        static struct Res_int32 res = {}; 

        BUS_INIT_CMD(res, EBusCmd_Boot_EraseFlashBlock);   

        if( cmd->xipOffset < BOOT_IMAGE_SIZE )
        {
            res.result = SDKErr_Fail;
        }
        else if( (cmd->xipOffset % FLASH_SECTOR_SIZE) != 0 )
        {
            res.result = SDKErr_Fail;
        }
        else
        {            
            flash_range_erase(cmd->xipOffset, FLASH_SECTOR_SIZE);
        }

        bus_tx_rpc_set_return_main(server->appTx, &res.header, &res.header);  
        break;
    }
    case EBusCmd_Boot_WriteFlashBlock:
    {
        struct Cmd_Boot_WriteFlashBlock* cmd = (struct Cmd_Boot_WriteFlashBlock*)frame;        
        static struct Res_int32 res = {}; 

        BUS_INIT_CMD(res, EBusCmd_Boot_WriteFlashBlock);   

        if( cmd->xipOffset < BOOT_IMAGE_SIZE )
        {
            res.result = SDKErr_Fail;
        }
        //else if( (cmd->xipOffset % FLASH_SECTOR_SIZE) != 0 )
        //{
          //  res.result = SDKErr_Fail;
        //}
        else if( (cmd->sz > FLASH_SECTOR_SIZE) || (cmd->sz > NUM_ELEMS(cmd->data)) )
        {
            res.result = SDKErr_Fail;
        }        
        else
        {            
            flash_range_program(cmd->xipOffset, cmd->data, cmd->sz);           
        }

        bus_tx_rpc_set_return_main(server->appTx, &res.header, &res.header);  
        break;
    }
    case EBusCmd_Boot_ReadFlashBlock:
    {
        struct Cmd_Boot_ReadFlashBlock* cmd = (struct Cmd_Boot_ReadFlashBlock*)frame;        
        static struct Res_Boot_ReadFlashBlock res = {}; 

        BUS_INIT_CMD(res, EBusCmd_Boot_ReadFlashBlock);   

        if( cmd->sz > FLASH_SECTOR_SIZE )
        {
            res.result = SDKErr_Fail;
        }

        const uint8_t* basePtr = ((const uint8_t*)XIP_BASE) + cmd->xipOffset;

        memcpy(res.data, basePtr, cmd->sz);

        bus_tx_rpc_set_return_main(server->appTx, &res.header, &res.header);          
        break;
    }
    default:
        break;
    }
}


//
//
int bootserver_init(struct BootServer_t* server, struct BootServerOptions_t* options)
{
    picocom_hw_init_led_only(false);

    memset(server, 0, sizeof(*server));

    if(options->forwardBusRx)
    {
        bus_rx_set_callback(options->forwardBusRx, boot_forward_handler_realtime, boot_forward_handler_main);
        options->forwardBusRx->userData = server;
    }

    bus_rx_set_callback(options->appRx, boot_app_handler_realtime, boot_app_handler_main);
    options->appRx->userData = server;
    
    server->coreId = options->coreId;
    server->forwardBusTx = options->forwardBusTx;
    server->forwardBusRx = options->forwardBusRx;
    server->appTx = options->appTx;
    server->appRx = options->appRx; 
    server->running = true;

    return SDKErr_OK;
}


int bootserver_wait_with_timeout( struct BootServer_t* server, uint32_t timeout )
{
    uint32_t start = picocom_time_ms_32();
    uint32_t* bootCheck = (uint32_t*)BOOT_CHECK_ADDR;
    server->lockCore = timeout > 0;
    while(server->running)
    {
        // process rx queues
        if(server->forwardBusRx)
            bus_rx_update(server->forwardBusRx);
        bus_rx_update(server->appRx);

        // process tx queues
        if(server->forwardBusTx)
            bus_tx_update(server->forwardBusTx);
        bus_tx_update(server->appTx);
          
#ifdef PICOCOM_SDL        
        picocom_sleep_us(1);
#endif  
        if( server->lockCore == false && timeout > 0 )
        {
            if( (picocom_time_ms_32() - start) > timeout)
                return SDKErr_OK;
        }

        picocom_wdt();  

        if(bootCheck)
        {
            if( (*bootCheck) == 0x69)
                break;
        }
    }
    return SDKErr_Fail;
}


//
//
int bootclient_init(struct BootClient_t* client, enum EBootTargetCore core, bool lockCore)
{
    memset(client, 0, sizeof(*client));

    switch (core)
    {
    case EBootTargetCore_VDP1:
    {  
        bus_tx_configure(
            &client->busTx,
            APP_VLNK_TX_PIO,
            APP_VLNK_TX_SM,
            APP_VLNK_TX_DATA_CNT,
            APP_VLNK_TX_D0_PIN,
            APP_VLNK_TX_ACK_PIN,
            APP_VLNK_TX_IRQ,
            APP_VLNK_TX_DIV
        );
        client->busTx.name = "(app)vdp1_vlnk_tx";
        client->busTx.max_tx_size = BUS_MAX_PACKET_DMA_SIZE; // display.h defined DISPLAY_GPU_CMD_BUS_MAX_PACKET_SZ, must be <= BUS_MAX_PACKET_DMA_SIZE to match hw
        
        bus_tx_init( &client->busTx );

        bus_rx_configure(&client->busRx,
            APP_VLNK_RX_PIO,
            APP_VLNK_RX_SM,       
            APP_VLNK_RX_DATA_CNT,
            APP_VLNK_RX_D0_PIN,
            APP_VLNK_RX_ACK_PIN
        );
        client->busRx.name = "(app)vdp1Link_rx";
        client->busRx.rx_buffer_size = APP_VLNK_RX_BUFFER_SZ;
        bus_rx_init(&client->busRx);                
        break;
    }
    case EBootTargetCore_APU:
    { 
        // Init bus
        bus_tx_configure(
            &client->busTx,
            APP_ALNK_TX_PIO,
            APP_ALNK_TX_SM,
            APP_ALNK_TX_DATA_CNT,
            APP_ALNK_TX_D0_PIN,
            APP_ALNK_TX_ACK_PIN,
            APP_ALNK_TX_IRQ,
            ALNK_DIV
        );
        client->busTx.name = "apuLink_tx";
        bus_tx_init( &client->busTx );

        bus_rx_configure(&client->busRx,
            APP_ALNK_RX_PIO,
            APP_ALNK_RX_SM,       
            APP_ALNK_RX_DATA_CNT,
            APP_ALNK_RX_D0_PIN,
            APP_ALNK_RX_ACK_PIN
        );
        bus_rx_init(&client->busRx);
        
        break;
    }
    default:
        return SDKErr_Fail;
        break;
    }

    if(lockCore)
    {
        // write bus lock cmd
        struct Res_uint32 nullCmd = {}; // any cmd will do
        BUS_INIT_CMD(nullCmd, EBusCmd_Boot_LockCore);            
        bus_tx_write_async(&client->busTx, (uint8_t*)&nullCmd.header, sizeof(nullCmd) );        
    }


    return SDKErr_OK;
}


int bootclient_write_uf2(struct BootClient_t* client, const uint8_t* bufffer, uint32_t sz, bool forwarded)
{
    int res;

    assert(sizeof(UF2DataBlock_t) == 512);
    int numBlocks = sz / sizeof(UF2DataBlock_t);
    struct UF2DataBlock_t* uf2 = (struct UF2DataBlock_t*)bufffer;

    // page erase tracker
    uint32_t pages[ (MAX_UF2_IMAGE_SIZE / 4096)/32 ] = {0};
    if(numBlocks > (NUM_ELEMS(pages)*32))
        return SDKErr_Fail;
    
    struct Cmd_Boot_WriteFlashBlock writeCmd;

    for(int i=0;i<numBlocks;i++)
    {
        // next block
        if(!uf2_init(uf2))
        {            
            return SDKErr_Fail;
        }
        
        if(uf2->payloadSize != 256)
        {
            return SDKErr_Fail;  
        }
        
        uint32_t targetAddr = uf2->targetAddr & 0x00ffffff;
        uint32_t absTargetAddress = XIP_BASE + targetAddr;

        // Skip out of range, uf2 contains 
        // Ideally should use flag but they seems to not match the spec
        if(targetAddr > PICO_FLASH_SIZE_BYTES)
        {
            uf2++;
            continue;
        }

        uint32_t xipOffset = targetAddr;

        // Protected boot image
        if( xipOffset < BOOT_IMAGE_SIZE)
        {
            return SDKErr_Fail;
        }

        // Get page        
        uint32_t pageId = xipOffset / 4096;

        // Page erase check
        uint32_t pageBitmaskId = pageId / 32;
        uint32_t bitId = pageId % 32;
        uint32_t pageSet = pages[pageBitmaskId] & (1 << bitId);

        if(!pageSet)
        {
            // set erased
            pages[pageBitmaskId] |= (1 << bitId);

            // erase block
            res = bootclient_erase_block( client, xipOffset, 4096, forwarded );    // erase block
            if(res != SDKErr_OK)    
                return res;
        }

        // write block
        memcpy(writeCmd.data, uf2->data, uf2->payloadSize);
        writeCmd.sz = uf2->payloadSize;

        res = bootclient_write_block( client, xipOffset, &writeCmd, forwarded );
        if(res != SDKErr_OK)    
            return res;
        
        uf2++;
    }
    
    return SDKErr_OK;
}


int bootclient_getStatus( struct BootClient_t* client, struct Cmd_Boot_GetStatus* statusOut, bool forwarded )
{
    // blocking status query    
    int res;
    struct Cmd_Boot_GetStatus statusCmd = {};    
    BUS_INIT_CMD(statusCmd, EBusCmd_Boot_GetStatus);
    if(forwarded)
        statusCmd.header.flags |= EBusStatusFlags_User0;
    res = bus_tx_request_blocking(&client->busTx, &client->busRx, &statusCmd.header, &statusOut->header, sizeof(*statusOut), 10000 );
    if(res != SDKErr_OK)
    {
        return res;
    }

    return SDKErr_OK;
}


int bootclient_boot(struct BootClient_t* client, bool forwarded)
{
    // write bus lock cmd
    struct Res_uint32 nullCmd = {}; 
    BUS_INIT_CMD(nullCmd, EBusCmd_Boot_BootApplication);
    if(forwarded)
        nullCmd.header.flags |= EBusStatusFlags_User0;    
    bus_tx_write_async(&client->busTx, (uint8_t*)&nullCmd.header, sizeof(nullCmd) );
    return SDKErr_OK;
}


int bootclient_erase_block(struct BootClient_t* client, uint32_t xipOffset, uint32_t sz, bool forwarded )
{
    // blocking status query    
    int res;
    struct Cmd_Boot_EraseFlashBlock flashCmd = {};    
    BUS_INIT_CMD(flashCmd, EBusCmd_Boot_EraseFlashBlock);
    if(forwarded)
        flashCmd.header.flags |= EBusStatusFlags_User0; 
    flashCmd.xipOffset = xipOffset;
    flashCmd.sz = sz;

    struct Res_int32 resCodeOut = {}; 
    res = bus_tx_request_blocking(&client->busTx, &client->busRx, &flashCmd.header, &resCodeOut.header, sizeof(resCodeOut), 10000 );
    if(res != SDKErr_OK)
    {
        return res;
    }

    return resCodeOut.result;
}


int bootclient_write_block(struct BootClient_t* client, uint32_t xipOffset, struct Cmd_Boot_WriteFlashBlock* block, bool forwarded )
{    
    int res;    
    BUS_INIT_CMD_PTR(block, EBusCmd_Boot_WriteFlashBlock);
    if(forwarded)
        block->header.flags |= EBusStatusFlags_User0;     
    block->xipOffset = xipOffset;

    struct Res_int32 resCodeOut = {}; 
    res = bus_tx_request_blocking(&client->busTx, &client->busRx, &block->header, &resCodeOut.header, sizeof(resCodeOut), 10000 );
    if(res != SDKErr_OK)
    {
        return res;
    }

    return resCodeOut.result;
}


int bootclient_read_block(struct BootClient_t* client, uint32_t xipOffset, struct Res_Boot_ReadFlashBlock* block, uint32_t sz, bool forwarded )
{
    int res;    
    struct Cmd_Boot_ReadFlashBlock flashCmd = {};    
    BUS_INIT_CMD(flashCmd, EBusCmd_Boot_ReadFlashBlock);
    if(forwarded)
        block->header.flags |= EBusStatusFlags_User0;     
    
    flashCmd.xipOffset = xipOffset;
    flashCmd.sz = sz;
    
    res = bus_tx_request_blocking(&client->busTx, &client->busRx, &flashCmd.header, &block->header, sizeof(*block), 10000 );
    if(res != SDKErr_OK)
    {
        return res;
    }

    return block->result;
}