#include "flash_store.h"
#include <stdio.h>
#include "picocom/utils/profiler.h"
#include "thirdparty/crc16/crc.h"


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
int flash_store_init_xip( struct FlashStore_t* store, uint32_t offset, uint32_t size )
{
    memset(store, 0, sizeof(*store));
 
    // ensure page aligned
    if( (offset % FLASH_SECTOR_SIZE) != 0 )
        return SDKErr_Fail;

#ifdef PICOCOM_PICO
    store->xipBase = (uint8_t*)XIP_BASE;  
    store->xipEnd = store->xipBase + PICO_FLASH_SIZE_BYTES;    
#else
    store->xipBase = picocom_malloc(PICO_FLASH_SIZE_BYTES);
    store->xipEnd = store->xipBase + PICO_FLASH_SIZE_BYTES;    
#endif
    printf("PICO_FLASH_SIZE_BYTES: %d\n", PICO_FLASH_SIZE_BYTES);

    store->flashTargetOffset = PICO_FLASH_SIZE_BYTES - size;
    store->arenaBasePtr = store->xipBase + store->flashTargetOffset;
    store->arenaSize = size;

    printf("store->flashTargetOffset: %d\n", store->flashTargetOffset);
    printf("store->arenaBasePtr: 0x%hhn\n", store->arenaBasePtr);
    printf("store->arenaSize: %d\n", store->arenaSize);

    return SDKErr_OK;
}


int flash_store_begin( struct FlashStore_t* store, uint32_t baseOffset )
{
    // ensure page aligned
    if( (baseOffset % FLASH_SECTOR_SIZE) != 0 )
        return SDKErr_Fail;

    store->flashWriteCnt++;
    if(store->flashWriteCnt > FLASH_MAX_WRITE)
    {                
        return SDKErr_Fail; // "flashWriteCnt > GPU_FLASH_MAX_WRITE"        
    }        

    store->currentOffset = baseOffset;
    store->currentPageBaseOffset = baseOffset;
    return SDKErr_OK;
}


static void call_flash_range_erase(void *param) 
{
    uint32_t offset = (uint32_t)param;
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}


static void call_flash_range_program(void *param) 
{
    uint32_t offset = ((uintptr_t*)param)[0];
    const uint8_t *data = (const uint8_t *)((uintptr_t*)param)[1];
    flash_range_program(offset, data, /*FLASH_PAGE_SIZE*/ FLASH_SECTOR_SIZE);
}


static int flash_store_flush( struct FlashStore_t* store )
{
    if(!store->tempBlockWriteCnt)
    {
        return SDKErr_OK;
    }
        
    if( store->flashTargetOffset + store->currentPageBaseOffset >= PICO_FLASH_SIZE_BYTES )
    {
        printf(" store->flashTargetOffset + store->currentPageBaseOffset >= PICO_FLASH_SIZE_BYTES\n");
        return SDKErr_Fail;     
    }

    int cmpRes = memcmp(store->tempFlashPage, store->arenaBasePtr + store->currentPageBaseOffset, FLASH_SECTOR_SIZE);
    uint16_t srcCrc = picocom_crc16(store->tempFlashPage, FLASH_SECTOR_SIZE);
    uint16_t dstCrc = picocom_crc16(store->arenaBasePtr + store->currentPageBaseOffset, FLASH_SECTOR_SIZE);
    if( cmpRes != 0 )
    {
        int offset = store->flashTargetOffset + store->currentPageBaseOffset;

        if((store->flashTargetOffset % 4096) != 0)
        {
            printf("store->flashTargetOffset %d not 4096 page aligned\n", store->flashTargetOffset );
            return SDKErr_Fail;
        }

        // Write current temp page into flash
        uint32_t ints = save_and_disable_interrupts();        
     
        flash_range_erase(offset, FLASH_SECTOR_SIZE);  
        flash_range_program(offset, store->tempFlashPage, FLASH_SECTOR_SIZE);            

        restore_interrupts( ints );
    }

    // clear on upload
    memset(store->tempFlashPage, 0, FLASH_SECTOR_SIZE );   
    store->tempBlockWriteCnt = 0;

    // next sector, could work this out like the gpu but its seq 
    store->currentPageBaseOffset += FLASH_SECTOR_SIZE;

    return SDKErr_OK;
}


int flash_store_next_write_block( struct FlashStore_t* store, uint32_t globalOffset, const uint8_t* data, uint32_t size)
{
#ifdef PICOCOM_SDL
    flash_simulator_set_xip_bases ((uint8_t*)store->xipBase, (uint8_t*)store->xipEnd);
#endif

    // ensure sequencial
    if( store->currentOffset != globalOffset)
        return SDKErr_Fail;

    // calc local offset
    uint32_t localOffsetInPage = globalOffset % FLASH_SECTOR_SIZE;

    // Ensure offset will fit remain
    if( localOffsetInPage > FLASH_SECTOR_SIZE - size || size > FLASH_SECTOR_SIZE)
        return SDKErr_Fail;

    // copy into page temp
    memcpy(store->tempFlashPage + localOffsetInPage, data, size );
    store->tempBlockWriteCnt++;

    // detect last write in page
    if( localOffsetInPage + size == FLASH_SECTOR_SIZE)
    {           
        int res = flash_store_flush( store );
        if(res != SDKErr_OK)
            return res;
    }

    // Next block
    store->currentOffset += size;

    return SDKErr_OK;
}


int flash_store_end( struct FlashStore_t* store )
{
#ifdef PICOCOM_SDL
    flash_simulator_set_xip_bases ((uint8_t*)store->xipBase, (uint8_t*)store->xipEnd);
#endif

    // flush last flash block
    return flash_store_flush( store );
}


uint8_t* flash_store_get_ptr( struct FlashStore_t* store, uint32_t offset)
{
    if( offset > store->arenaSize )
    {
        //printf("flash_store_get_ptr overflow\n");
        return 0;
    }
    return store->arenaBasePtr + offset;
}
