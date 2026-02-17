/** Low level Boot cmd interface. 
*/
#pragma once 


#include "platform/pico/bus/bus.h"
#include "lib/components/flash_store/flash_store.h"


// Config
#define MAX_UF2_IMAGE_SIZE (1024*1024*16)


/** VDP1 Bus commands
 */
enum EBusCmd_Boot
{    
    EBusCmd_Boot_GetStatus = EBusCmd_VDP1_BASE,    
    EBusCmd_Boot_LockCore,
    EBusCmd_Boot_BootApplication,
    EBusCmd_Boot_EraseFlashBlock,
    EBusCmd_Boot_WriteFlashBlock,
    EBusCmd_Boot_ReadFlashBlock,
};


/** Get device info */
typedef struct __attribute__((__packed__)) Cmd_Boot_GetStatus
{    
    struct Cmd_Header_t header;      
    
    uint8_t coreId;
    bool isReady;
    bool coreLocked;
} Cmd_Boot_GetStatus;


/** Erase flash block */
typedef struct __attribute__((__packed__)) Cmd_Boot_EraseFlashBlock
{    
    struct Cmd_Header_t header;      
    
    uint32_t xipOffset; // erase block, must be 4096 page block aligned (from XIP base eg. 0 for start of flash)
    uint32_t sz;        // erase size, must be 4096 
} Cmd_Boot_EraseFlashBlock;


/** Write flash block */
typedef struct __attribute__((__packed__)) Cmd_Boot_WriteFlashBlock
{    
    struct Cmd_Header_t header;      
    
    uint32_t xipOffset; // must be 4096 page block aligned (from XIP base eg. 0 for start of flash)
    uint32_t sz;        // write size, must be 4096 
    uint8_t data[FLASH_SECTOR_SIZE];
} Cmd_Boot_WriteFlashBlock;


/** Read flash block */
typedef struct __attribute__((__packed__)) Cmd_Boot_ReadFlashBlock
{    
    struct Cmd_Header_t header;      
    
    uint32_t xipOffset; // address to read (from XIP base)
    uint32_t sz;        // read size     
} Cmd_Boot_ReadFlashBlock;


/** Result for read flash block */
typedef struct __attribute__((__packed__)) Res_Boot_ReadFlashBlock
{    
    struct Cmd_Header_t header;      
    
    int result;
    uint32_t xipOffset; // address of read data (from XIP base)
    uint32_t sz;        // read size
    uint8_t data[FLASH_SECTOR_SIZE];  
} Res_Boot_ReadFlashBlock;
