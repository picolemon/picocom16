#pragma once

#include "picocom/platform.h"

// Flash config
#ifdef PICOCOM_PICO
    #include "pico/stdlib.h"
    #include <hardware/sync.h>
    #include <hardware/flash.h>
#else
    #define FLASH_SECTOR_SIZE (1u << 12)
    #define PICO_FLASH_SIZE_BYTES 0x00400000
#endif

// Flash defs
#define FLASH_MAX_WRITE 1024

/* Flash store state */
typedef struct FlashStore_t
{    
    uint8_t* xipBase;                           // Flash xip base or simulated ram ptr
    uint8_t* xipEnd;    
    uint32_t arenaSize;                         // Max size of arena eg. arenaBasePtr + arenaSize
    uint32_t flashTargetOffset;                 // Offset in flash and its related xipBase
    uint8_t* arenaBasePtr;                      // Base ptr of memory, all offsets are from this origin in ram or flash ( flash will be xip base with some offset ), basically xipBase + flashTargetOffset
    uint8_t tempFlashPage[FLASH_SECTOR_SIZE];     // Temp flash page
    uint32_t currentOffset;                     // current byte offset in writing stream ( sequencial writing )
    uint32_t currentPageBaseOffset;             // current byte offset ( page base offset in FLASH_SECTOR_SIZE increments )
    uint32_t flashWriteCnt;
    uint32_t tempBlockWriteCnt;
} FlashStore_t;


// Flash store apu
int flash_store_init_xip( struct FlashStore_t* store, uint32_t offset, uint32_t size );  // init flash at offset, multiple flash arenas can be created
int flash_store_begin( struct FlashStore_t* store, uint32_t baseOffset );  // Begin sequencial flash write, set base address ( must be page aligned, even for ram which is currently wasteful )
int flash_store_next_write_block( struct FlashStore_t* store, uint32_t globalOffset, const uint8_t* data, uint32_t size);   // Write block 
int flash_store_end( struct FlashStore_t* store ); // complete write, flush final page
uint8_t* flash_store_get_ptr( struct FlashStore_t* store, uint32_t offset);
