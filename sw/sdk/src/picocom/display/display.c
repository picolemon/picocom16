#include "picocom/devkit.h"
#include "lib/platform/pico/hw/picocom_hw.h"
#include "lib/components/vdp1_core/vdp_client.h"
#include "thirdparty/crc16/crc.h"
#include "display.h"
#include <stdio.h>
#include "thirdparty/miniz/miniz.h"
#include "picocom/utils/profiler.h"


// globals
struct DisplayState_t* g_DisplayState = 0;
uint16_t debugColors[3] = { 0b1111100000000000, 0b1111100000011111, 0b0000011111100000 };    


// Fwd
void test_service_vdp1_main(void* userData);


//
//
// Display api
struct DisplayOptions_t display_default_options()
{
    struct DisplayOptions_t options = {
        .maxPacketSize = DISPLAY_GPU_CMD_BUS_MAX_PACKET_SZ,
        .cmdListAllocSize = DISPLAY_GPU_CMD_ALLOC_SZ
    };
    return options;
}


int display_init()
{
    struct DisplayOptions_t options = display_default_options();
    return display_init_with_options(&options);
}


int display_init_with_options(struct DisplayOptions_t* options)
{
    if(g_DisplayState)
        return SDKErr_Fail;

    g_DisplayState = (struct DisplayState_t*)picocom_malloc(sizeof(struct DisplayState_t));
    memset(g_DisplayState, 0, sizeof(DisplayState_t));

    BusTx_t* vdp1Link_tx = &g_DisplayState->vdp1Link_tx;
    BusRx_t* vdp1Link_rx = &g_DisplayState->vdp1Link_rx;
    
    bus_tx_configure(
        vdp1Link_tx,
        APP_VLNK_TX_PIO,
        APP_VLNK_TX_SM,
        APP_VLNK_TX_DATA_CNT,
        APP_VLNK_TX_D0_PIN,
        APP_VLNK_TX_ACK_PIN,
        APP_VLNK_TX_IRQ,
        APP_VLNK_TX_DIV
    );
    vdp1Link_tx->name = "(app)vdp1_vlnk_tx";
    vdp1Link_tx->max_tx_size = BUS_MAX_PACKET_DMA_SIZE; // display.h defined DISPLAY_GPU_CMD_BUS_MAX_PACKET_SZ, must be <= BUS_MAX_PACKET_DMA_SIZE to match hw
    
    bus_tx_init( vdp1Link_tx );

    bus_rx_configure(vdp1Link_rx,
        APP_VLNK_RX_PIO,
        APP_VLNK_RX_SM,       
        APP_VLNK_RX_DATA_CNT,
        APP_VLNK_RX_D0_PIN,
        APP_VLNK_RX_ACK_PIN
    );
    vdp1Link_rx->name = "(app)vdp1Link_rx";
    vdp1Link_rx->rx_buffer_size = APP_VLNK_RX_BUFFER_SZ;
    bus_rx_init(vdp1Link_rx);
    
    struct VdpClientInitOptions_t vdpOptions = {0};
    vdpOptions.vdp1Link_tx = vdp1Link_tx;
    vdpOptions.vdp1Link_rx = vdp1Link_rx;        
    vdpOptions.cmdListPoolCnt = 2;          // number of cmd lists to allow in flight
    vdpOptions.cmdListAllocSize = options->cmdListAllocSize;   // cmd list size sould match max bus packet
    
    g_DisplayState->client = vdp1_client_init(&vdpOptions);
    if(!g_DisplayState->client)
        return SDKErr_Fail;

    return SDKErr_OK;
}


int display_update()
{
    if(!g_DisplayState || !g_DisplayState->client)
        return SDKErr_Fail;    
    return vdp1_update_queue(g_DisplayState->client);
}


int display_reset_gpu()
{
    int res;

    if(!g_DisplayState || !g_DisplayState->client)
        return SDKErr_Fail;    

    // reset bus
    struct Res_uint32 nullCmd = {}; // any cmd will do
    BUS_INIT_CMD(nullCmd, EBusCmd_VDP1_ResetBus);
    bus_tx_write_async(g_DisplayState->client->vdp1Link_tx, (uint8_t*)&nullCmd.header, sizeof(nullCmd) );

    vdp1_begin_frame(g_DisplayState->client, 1, 0);

    struct GPUCMD_ResetGpu* resetCmd = (GPUCMD_ResetGpu*)vdp1_cmd_add_next(g_DisplayState->client, 0, sizeof(GPUCMD_ResetGpu));
    if(!resetCmd)
        return SDKErr_Fail;
    GPU_INIT_CMD(resetCmd, EGPUCMD_ResetGpu);        
    resetCmd->cmds = 1;
    resetCmd->buffers = 1;
    resetCmd->stats = 1;

    vdp1_end_frame(g_DisplayState->client, true, false);  

    return SDKErr_OK;
}


struct VdpClientImpl_t* display_get_impl()
{
    if(!g_DisplayState || !g_DisplayState->client)
        return 0;    

    return g_DisplayState->client;
}


struct DisplayState_t* display_get_state()
{
    return g_DisplayState;
}


int display_upload_buffer(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena, uint32_t memOffset, uint8_t textureFormat, uint16_t w, uint16_t h )
{
    int res = SDKErr_Fail;

    if(vdpId == 0)
    {
        if(!g_DisplayState || !g_DisplayState->client)
            return SDKErr_Fail;    

        uint32_t start = picocom_time_us_32();
          
        vdp1_begin_frame(g_DisplayState->client, 1, 0);

        struct GPUCMD_CreateBuffer* createCmd = (GPUCMD_CreateBuffer*)vdp1_cmd_add_next(g_DisplayState->client, 0, sizeof(GPUCMD_CreateBuffer));
        if(!createCmd)
            return SDKErr_OK;

        GPU_INIT_CMD(createCmd, EGPUCMD_CreateBuffer);      
        createCmd->bufferId = bufferId;
        createCmd->arena = arena;
        createCmd->memOffset = memOffset;
        createCmd->memSize = sz;
        createCmd->textureFormat = textureFormat;
        createCmd->w = w;
        createCmd->h = h;    
        createCmd->parentBufferId = 0xffff;
        createCmd->header.cullTileMask = 0b1; // tile 1
        
        res = vdp1_end_frame(g_DisplayState->client, true, false);
        if(res != SDKErr_OK)
            return res;

        if(buffer && sz)
            return display_update_buffer_data(bufferId, vdpId, buffer, sz, arena);

    }
    else if(vdpId == 1) // VDP2 upload through VDP1
    {
        if(!g_DisplayState || !g_DisplayState->client)
            return SDKErr_Fail;    

        uint32_t start = picocom_time_us_32();
          
        vdp1_begin_frame(g_DisplayState->client, 1, 0);
        
        struct GPUCMD_CreateBuffer* createCmd = (GPUCMD_CreateBuffer*)vdp1_cmd_add_next_impl(g_DisplayState->client, 0, sizeof(GPUCMD_CreateBuffer), false); // Add cmd but dont flush
        if(!createCmd)
            return SDKErr_OK;

        GPU_INIT_CMD(createCmd, EGPUCMD_CreateBuffer);      
        createCmd->bufferId = bufferId;
        createCmd->arena = arena;
        createCmd->memOffset = memOffset;
        createCmd->memSize = sz;
        createCmd->textureFormat = textureFormat;
        createCmd->w = w;
        createCmd->h = h;    
        createCmd->parentBufferId = 0xffff;         
        createCmd->header.cullTileMask = 0b1; // tile 1

        res = vdp1_end_frame_commit_to_vdp2(g_DisplayState->client, true);
        if(res != SDKErr_OK)
            return res;

        if(buffer && sz)
            return display_update_buffer_data(bufferId, vdpId, buffer, sz, arena);            
    }

    return res;
}


int display_upload_buffer_compressed(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint32_t compressedSize, uint8_t arena, uint32_t memOffset, uint8_t textureFormat, uint16_t w, uint16_t h )
{
    int res = SDKErr_Fail;

    if(vdpId == 0)
    {
        if(!g_DisplayState || !g_DisplayState->client)
            return SDKErr_Fail;    

        uint32_t start = picocom_time_us_32();
          
        vdp1_begin_frame(g_DisplayState->client, 1, 0);

        struct GPUCMD_CreateBuffer* createCmd = (GPUCMD_CreateBuffer*)vdp1_cmd_add_next(g_DisplayState->client, 0, sizeof(GPUCMD_CreateBuffer));
        if(!createCmd)
            return SDKErr_OK;

        GPU_INIT_CMD(createCmd, EGPUCMD_CreateBuffer);      
        createCmd->bufferId = bufferId;
        createCmd->arena = arena;
        createCmd->memOffset = memOffset;
        createCmd->memSize = sz;
        createCmd->textureFormat = textureFormat;
        createCmd->w = w;
        createCmd->h = h;    
        createCmd->parentBufferId = 0xffff;
        createCmd->header.cullTileMask = 0b1; // tile 1
        
        res = vdp1_end_frame(g_DisplayState->client, true, false);
        if(res != SDKErr_OK)
            return res;

        if(buffer && sz)
        {
            res = display_update_buffer_data_compressed(bufferId, vdpId, buffer, compressedSize, arena, 0);            
            return res;
        }

    }
    else if(vdpId == 1) // VDP2 upload through VDP1
    {
        if(!g_DisplayState || !g_DisplayState->client)
            return SDKErr_Fail;    

        uint32_t start = picocom_time_us_32();
          
        vdp1_begin_frame(g_DisplayState->client, 1, 0);
        
        struct GPUCMD_CreateBuffer* createCmd = (GPUCMD_CreateBuffer*)vdp1_cmd_add_next_impl(g_DisplayState->client, 0, sizeof(GPUCMD_CreateBuffer), false); // Add cmd but dont flush
        if(!createCmd)
            return SDKErr_OK;

        GPU_INIT_CMD(createCmd, EGPUCMD_CreateBuffer);      
        createCmd->bufferId = bufferId;
        createCmd->arena = arena;
        createCmd->memOffset = memOffset;
        createCmd->memSize = sz;
        createCmd->textureFormat = textureFormat;
        createCmd->w = w;
        createCmd->h = h;    
        createCmd->parentBufferId = 0xffff;         
        createCmd->header.cullTileMask = 0b1; // tile 1

        res = vdp1_end_frame_commit_to_vdp2(g_DisplayState->client, true);
        if(res != SDKErr_OK)
            return res;

        if(buffer && sz)
            return display_update_buffer_data_compressed(bufferId, vdpId, buffer, compressedSize, arena, 0);            
    }

    return res;
}


int display_create_buffer(uint8_t bufferId, uint8_t vdpId, uint32_t sz, uint8_t arena, uint32_t memOffset, uint8_t textureFormat, uint16_t w, uint16_t h, uint16_t parentBufferId)
{
    int res = SDKErr_Fail;

    if(vdpId == 0)
    {
        if(!g_DisplayState || !g_DisplayState->client)
            return SDKErr_Fail;    

        uint32_t start = picocom_time_us_32();
          
        vdp1_begin_frame(g_DisplayState->client, 1, 0);

        struct GPUCMD_CreateBuffer* createCmd = (GPUCMD_CreateBuffer*)vdp1_cmd_add_next(g_DisplayState->client, 0, sizeof(GPUCMD_CreateBuffer));
        if(!createCmd)
            return SDKErr_OK;

        GPU_INIT_CMD(createCmd, EGPUCMD_CreateBuffer);      
        createCmd->bufferId = bufferId;
        createCmd->arena = arena;
        createCmd->memOffset = memOffset;
        createCmd->memSize = sz;
        createCmd->textureFormat = textureFormat;
        createCmd->w = w;
        createCmd->h = h;  
        createCmd->parentBufferId = parentBufferId;  
        createCmd->header.cullTileMask = 0b1; // tile 1

        res = vdp1_end_frame(g_DisplayState->client, true, false);
        if(res != SDKErr_OK)
            return res;
    }
    else if(vdpId == 1)
    {
        // NOTE: lots of duplication as really really dont want to break vdp1 as its all very fragile
        // want to re-use command buffering as its going to the same place

        if(!g_DisplayState || !g_DisplayState->client)
            return SDKErr_Fail;    

        uint32_t start = picocom_time_us_32();
          
        vdp1_begin_frame(g_DisplayState->client, 1, 0);
        
        struct GPUCMD_CreateBuffer* createCmd = (GPUCMD_CreateBuffer*)vdp1_cmd_add_next_impl(g_DisplayState->client, 0, sizeof(GPUCMD_CreateBuffer), false); // Add cmd but dont flush
        if(!createCmd)
            return SDKErr_OK;

        GPU_INIT_CMD(createCmd, EGPUCMD_CreateBuffer);      
        createCmd->bufferId = bufferId;
        createCmd->arena = arena;
        createCmd->memOffset = memOffset;
        createCmd->memSize = sz;
        createCmd->textureFormat = textureFormat;
        createCmd->w = w;
        createCmd->h = h;    
        createCmd->parentBufferId = parentBufferId;
        createCmd->header.cullTileMask = 0b1; // tile 1

        res = vdp1_end_frame_commit_to_vdp2(g_DisplayState->client, true);
        if(res != SDKErr_OK)
            return res;      
    }

    return res;
}


int display_update_buffer_data(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena)
{
    return display_update_buffer_data_ex( bufferId, vdpId, buffer, sz, arena, 0 );
}


int display_update_buffer_data_ex(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena, uint32_t offsetInBuffer)
{
    int res = SDKErr_Fail;

    if(vdpId == 0)
    {
#ifdef GPU_TRACE_CMD_BUFFERS
        printf("display_update_buffer_data bufferId: %d, sz: %d, crc: 0x%X\n", bufferId, sz, picocom_crc16(buffer, sz));
        for(int i=0;i<sz;i++)
        {
            printf("%02X ",buffer[i]);
        }
        printf("\n");
#endif

        // write blob as 512b blocks
        int remain = sz;
        int readOffset = 0;

        // max send size aligned to flash pages
        int blockSz = g_DisplayState->client->maxCmdSize-sizeof(GPUCMD_WriteBufferData);
        if(blockSz > sz)
            blockSz = sz;
        if(blockSz > GPU_FLASH_BUFFER_PAGE_SIZE)
            blockSz = GPU_FLASH_BUFFER_PAGE_SIZE;

        uint32_t pageSize = 0;

        while(remain > 0)
        {                 
            bool isLast = false;

            if(remain <= blockSz)
            {
                blockSz = remain;
                isLast = true;
            }

            res = vdp1_begin_frame(g_DisplayState->client, 1, 0);
            if(res != SDKErr_OK)
                return res;   

            struct GPUCMD_WriteBufferData* writeCmd = (GPUCMD_WriteBufferData*)vdp1_cmd_add_next(g_DisplayState->client, 0, sizeof(GPUCMD_WriteBufferData) + blockSz);
            if(!writeCmd)
                picocom_panic(SDKErr_Fail, "!tex upload alloc fail");
            
            GPU_INIT_CMD(writeCmd, EGPUCMD_WriteBufferData);        
            writeCmd->header.sz = sizeof(GPUCMD_WriteBufferData) + blockSz; 
            writeCmd->offset = readOffset + offsetInBuffer;
            writeCmd->bufferId = bufferId;
            writeCmd->flags = 0;
            writeCmd->header.cullTileMask = 0b1; // tile 1

            // Commit every 4k
            if(arena == EGPUBufferArena_Flash0)
            {
                if(remain == sz)
                    writeCmd->flags |= EGPUCMD_WriteBufferDataFlags_firstPage;

                if( pageSize+blockSz >= GPU_FLASH_BUFFER_PAGE_SIZE || isLast )
                {
                    writeCmd->flags |= EGPUCMD_WriteBufferDataFlags_commitPage;
                    pageSize = 0;
                }
            }

            if(isLast)
            {
                // Mark last
                writeCmd->flags |= EGPUCMD_WriteBufferDataFlags_finalPage;

                // Lock buffer for writing
                if(arena == EGPUBufferArena_Flash0)
                {
                    writeCmd->flags |= EGPUCMD_WriteBufferDataFlags_lockWrites;
                }
            }

            // write
            writeCmd->dataSize = blockSz;
            for(int i=0;i<blockSz;i++)
            {
                writeCmd->data[i] = buffer[readOffset+i];            
            }

            remain -= blockSz;            
            readOffset += blockSz;
            pageSize += blockSz;

    #ifdef GPU_TRACE_CMD_BUFFERS
                printf("\t upload block bufferId: %d, offset: %d, sz: %d, crc: 0x%X\n", writeCmd->bufferId, writeCmd->offset, writeCmd->dataSize, picocom_crc16(writeCmd->data, writeCmd->dataSize));
                for(int i=0;i<writeCmd->dataSize;i++)
                {
                    printf("%02X ", writeCmd->data[i]);
                }
                printf("\n");
    #endif
     
            res = vdp1_end_frame(g_DisplayState->client, true, false);           
            if(res != SDKErr_OK)
                return res;        
        }

    #ifdef GPU_TRACE_CMD_BUFFERS
        //display_debug_dump_vdp_state();	
    #endif

        return SDKErr_OK;
    }
    else if(vdpId == 1)
    {
#ifdef GPU_TRACE_CMD_BUFFERS
        printf("display_update_buffer_data bufferId: %d, sz: %d, crc: 0x%X\n", bufferId, sz, picocom_crc16(buffer, sz));
        for(int i=0;i<sz;i++)
        {
            printf("%02X ",buffer[i]);
        }
        printf("\n");
#endif

        // write blob as 512b blocks
        int remain = sz;
        int readOffset = 0;

        // max send size aligned to flash pages
        int blockSz = g_DisplayState->client->maxCmdSize-sizeof(GPUCMD_WriteBufferData);
        if(blockSz > sz)
            blockSz = sz;
        if(blockSz > GPU_FLASH_BUFFER_PAGE_SIZE)
            blockSz = GPU_FLASH_BUFFER_PAGE_SIZE;

        uint32_t pageSize = 0;

        while(remain > 0)
        {                 
            bool isLast = false;

            if(remain <= blockSz)
            {
                blockSz = remain;
                isLast = true;
            }

            res = vdp1_begin_frame(g_DisplayState->client, 1, 0);
            if(res != SDKErr_OK)
                return res;   

            struct GPUCMD_WriteBufferData* writeCmd = (GPUCMD_WriteBufferData*)vdp1_cmd_add_next(g_DisplayState->client, 0, sizeof(GPUCMD_WriteBufferData) + blockSz);
            if(!writeCmd)
                picocom_panic(SDKErr_Fail, "!tex upload alloc fail");
            
            GPU_INIT_CMD(writeCmd, EGPUCMD_WriteBufferData);        
            writeCmd->header.sz = sizeof(GPUCMD_WriteBufferData) + blockSz; 
            writeCmd->offset = readOffset + offsetInBuffer;
            writeCmd->bufferId = bufferId;
            writeCmd->flags = 0;
            writeCmd->header.cullTileMask = 0b1; // tile 1

            // Commit every 4k
            if(arena == EGPUBufferArena_Flash0)
            {
                if(remain == sz)
                    writeCmd->flags |= EGPUCMD_WriteBufferDataFlags_firstPage;

                if(pageSize+blockSz >= GPU_FLASH_BUFFER_PAGE_SIZE || isLast )
                {
                    writeCmd->flags |= EGPUCMD_WriteBufferDataFlags_commitPage;
                    pageSize = 0;
                }
            }

            if(isLast)
            {
                // Mark last
                writeCmd->flags |= EGPUCMD_WriteBufferDataFlags_finalPage;

                // Lock buffer for writing
                if(arena == EGPUBufferArena_Flash0)
                {
                    writeCmd->flags |= EGPUCMD_WriteBufferDataFlags_lockWrites;
                }
            }

            // write
            writeCmd->dataSize = blockSz;
            for(int i=0;i<blockSz;i++)
            {
                writeCmd->data[i] = buffer[readOffset+i];            
            }

            remain -= blockSz;            
            readOffset += blockSz;
            pageSize += blockSz;

    #ifdef GPU_TRACE_CMD_BUFFERS
                printf("\t upload block bufferId: %d, offset: %d, sz: %d, crc: 0x%X\n", writeCmd->bufferId, writeCmd->offset, writeCmd->dataSize, picocom_crc16(writeCmd->data, writeCmd->dataSize));
                for(int i=0;i<writeCmd->dataSize;i++)
                {
                    printf("%02X ", writeCmd->data[i]);
                }
                printf("\n");
    #endif

            res = vdp1_end_frame_commit_to_vdp2(g_DisplayState->client, true);  
            if(res != SDKErr_OK)
                return res;        
        }

    #ifdef GPU_TRACE_CMD_BUFFERS
        //display_debug_dump_vdp_state();	
    #endif

        return SDKErr_OK;
    }

    return res;
}


int display_update_buffer_data_compressed(uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena, uint32_t memOffset)
{
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
        int start =  picocom_time_ms_32();        
            res = display_update_buffer_data_ex( bufferId, vdpId, pUncomp, uncomp_len, arena, memOffset + readMemOffset);
        //printf("Took: %d, sz: %d\n", picocom_time_ms_32()-start,sz);        
        if(res != SDKErr_OK)    
            return res;

        // Next block
        bufferOffset += blockSize;
        readMemOffset += uncomp_len;
    }

    return res;
}


int display_register_buffer_cmd(uint8_t bufferId, uint8_t vdpId, uint8_t cmdId)
{        
    if(!g_DisplayState || !g_DisplayState->client)
        return SDKErr_Fail;    

    vdp1_begin_frame(g_DisplayState->client, 1, 0);

    struct GPUCMD_RegisterCmd* registerCmd = (GPUCMD_RegisterCmd*)vdp1_cmd_add_next(g_DisplayState->client, 0, sizeof(GPUCMD_RegisterCmd));
    if(!registerCmd)
        picocom_panic(SDKErr_Fail, "!register cmd upload alloc fail");
    registerCmd->vdpId = vdpId;
    GPU_INIT_CMD(registerCmd, EGPUCMD_RegisterCmd);            
    registerCmd->bufferId = bufferId;
    registerCmd->cmdId = cmdId;

    vdp1_end_frame(g_DisplayState->client, true, false); 
    
    return SDKErr_OK;    
}


int display_upload_shader_impl(uint8_t cmd, uint8_t bufferId, uint8_t vdpId, const uint8_t* buffer, uint32_t sz, uint8_t arena, uint32_t memOffset, uint8_t textureFormat, uint16_t w, uint16_t h)
{
    int res = 0;

    res = display_upload_buffer(bufferId, vdpId, buffer, sz, arena, memOffset, textureFormat, w, h); 
    if(res != SDKErr_OK)
        return res;

    res = display_register_buffer_cmd(bufferId, vdpId, cmd); 
    if(res != SDKErr_OK)
        return res;

    return res;    
}


int display_set_profiler_enabled(bool enabled, uint32_t level)
{
    int res;

    uint32_t defaultBusTimeout = 0;
    PicocomGlobalState_t* inst = picocom_get_instance(); 
    if(inst)
    {
        defaultBusTimeout = inst->defaultBusTimeout;
    }

    if(!g_DisplayState || !g_DisplayState->client)
        return SDKErr_Fail;    

    // Get current gpu config
    VDP1CMD_Config configCmd = {};
    BUS_INIT_CMD(configCmd, EBusCmd_VDP1_GetConfig);

    VDP1CMD_Config currentConfig = {};
#ifdef PICOCOM_NATIVE_SIM          
    res = bus_tx_request_blocking_ex(g_DisplayState->client->vdp1Link_tx, g_DisplayState->client->vdp1Link_rx, &configCmd.header, &currentConfig.header, sizeof(currentConfig), 
        defaultBusTimeout, test_service_vdp1_main, 0 );
#else
    res = bus_tx_request_blocking(g_DisplayState->client->vdp1Link_tx, g_DisplayState->client->vdp1Link_rx, &configCmd.header, &currentConfig.header, sizeof(currentConfig), defaultBusTimeout );
#endif
    if(res != SDKErr_OK)
        return res;

    // Update profiler
    currentConfig.profilerEnabled = enabled;
    currentConfig.profilerLevel = level;

    // Re-send with config change
    VDP1CMD_Config responseConfig = {};
    BUS_INIT_CMD(currentConfig, EBusCmd_VDP1_SetConfig);
#ifdef PICOCOM_NATIVE_SIM              
    res = bus_tx_request_blocking_ex(g_DisplayState->client->vdp1Link_tx, g_DisplayState->client->vdp1Link_rx, &currentConfig.header, &responseConfig.header, sizeof(currentConfig), 
        defaultBusTimeout, test_service_vdp1_main, 0 );
#else
    res = bus_tx_request_blocking(g_DisplayState->client->vdp1Link_tx, g_DisplayState->client->vdp1Link_rx, &currentConfig.header, &responseConfig.header, sizeof(currentConfig), 
        defaultBusTimeout );
#endif        
    if(res != SDKErr_OK)
    {
        return res;
    }

    return SDKErr_OK;
}


DisplayStats_t* display_stats()
{
    if(!g_DisplayState)
        return 0;
    g_DisplayState->stats.avgFps = display_calc_avg_fps(&g_DisplayState->stats);

    return &g_DisplayState->stats;
}


static void display_print_vdp_frame_profile(GpuFrameProfile* stats)
{    
    /*const char* indent= "\t\t";
    if(!stats->isValid)
        return;
    printf("%sframeId: %d, frameCullCounts: %d\n", indent, stats->cmdSeqNum, stats->frameCullCounts);    
    for(int i=0;i<GPU_MAX_SHADER_CMD_ID;i++)
    {
        if(stats->cmdExecCounts[i] == 0)
            continue;
        
        const char* cmdName = 0;
        
        printf("%s\t" "cmd[", indent);
        if(g_DisplayState->gpuState && g_DisplayState->gpuState->cmdInfos[i].name )
        {
            printf("%s", g_DisplayState->gpuState->cmdInfos[i].name);
        }
        else 
        {
            printf("%d", i);
        }
        printf("] cnt:%d, max:%duS\n", stats->cmdExecCounts[i], stats->cmdMaxTime[i]);
    }*/
}


float display_calc_avg_fps(DisplayStats_t* stats)
{
    // move to fun, dont calc here
    uint64_t total = 0;
    for(int i=0;i<Display_Impl_State_MaxFrameTimeSamples;i++)
    {
        total += g_DisplayState->frameTimes[i];
    }
    return 1.0f / ((total / Display_Impl_State_MaxFrameTimeSamples) / 1000000.0f);
}


static void display_print_vdp_frame_stats(GpuFrameStats_t* stats)
{   
    const char* indent= "\t\t";
    if(!stats->isValid)
        return;     
    printf("%s" "cmdSeqNum: %d\n", indent, stats->cmdSeqNum);
    printf("%s" "frameTime: %duS\n", indent, stats->frameTime);
    printf("%s" "tileCnt: %d\n", indent, stats->tileCnt);
    printf("%s" "tileMaxTime: %duS\n", indent, stats->tileMaxTime);    
    printf("%s" "cmdErrors: %d\n", indent, stats->cmdErrors);
}   


void display_print_stats()
{
    DisplayStats_t* stats = display_stats();	

    printf("Frame[%d] stats fps: %f, lastT: %duS, minT: %duS, maxT: %duS\n", stats->frameId, stats->avgFps, stats->lastFrameTime, stats->minFrameTime, stats->maxFrameTime);
    printf("\tstats lastCmdSubmitSz: %d, lastCmdSubmitPacketCnt: %d, lastCmdSubmitCnt: %d\n", stats->lastCmdSubmitSz, stats->lastCmdSubmitPacketCnt, stats->lastCmdSubmitCnt);
    for(int i=0;i<2;i++)
    {
        if(stats->gpuFrameStats[i].isValid)
        {
            printf("\tvdp.framestats[%d]\n", i);
            display_print_vdp_frame_stats(&stats->gpuFrameStats[i]);
        }
        if(stats->gpuFrameProfile[i].isValid)
        {
            printf("\tvdp.frameprofile[%d]\n", i);
            display_print_vdp_frame_profile(&stats->gpuFrameProfile[i]);
        }        
    }   
}


int display_begin_frame()
{
    g_DisplayState->frameStartTime = picocom_time_us_32();
    return SDKErr_OK;
}


int display_end_frame()
{
    if(!g_DisplayState)
        return SDKErr_Fail;

    // frame time (total round trip)
    g_DisplayState->stats.lastFrameTime = picocom_time_us_32() - g_DisplayState->frameStartTime;
    if( g_DisplayState->stats.lastFrameTime > g_DisplayState->stats.maxFrameTime )
        g_DisplayState->stats.maxFrameTime = g_DisplayState->stats.lastFrameTime;
    if( g_DisplayState->stats.lastFrameTime < g_DisplayState->stats.minFrameTime )
        g_DisplayState->stats.minFrameTime = g_DisplayState->stats.lastFrameTime;    

    // append total frame sample ( all cmds )
    int index = g_DisplayState->frameTimeIndex % Display_Impl_State_MaxFrameTimeSamples;
    g_DisplayState->frameTimeIndex++;
    g_DisplayState->frameTimes[ index ] = g_DisplayState->stats.lastFrameTime;

    DisplayStats_t* stats = display_stats();
    stats->frameId++;

    return SDKErr_OK;
}


void display_debug_dump_vdp_state()
{
    int res;

    uint32_t defaultBusTimeout = 0;
    PicocomGlobalState_t* inst = picocom_get_instance(); 
    if(inst)
    {
        defaultBusTimeout = inst->defaultBusTimeout;
    }

    if(!g_DisplayState || !g_DisplayState->client)
        return;    

    // Get current gpu config
    VDP1CMD_DebugDump debugCmd = {};
    BUS_INIT_CMD(debugCmd, EBusCmd_VDP1_DebugDump);
    bus_tx_write_cmd_async(g_DisplayState->client->vdp1Link_tx, &debugCmd.header);
}
