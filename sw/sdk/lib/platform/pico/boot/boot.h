/** Boot server exists in the persistent boot section of the core, handles boot commands
 * from app or parent core (if chained)
 * - boot wait loop ( with optional timeout )
 * - protected flash read/write commands to install boot application
 * - flash functions 
 * - boot client for running commands
*/
#pragma once

#include "picocom/platform.h"
#include "platform/pico/boot/hw_boot_types.h"
#include "platform/pico/bus/bus.h"


// Config
#define BOOT_IMAGE_SIZE   (32*1024)                     // Bootloader size
#define BOOT_CHECK_ADDR   (0x400d8000 + 0x0c)           // Boot skip monitor ( SCRATCH0 )
#define BOOT_MAX_RX_BUFFER_SIZE 1024*8                  // Max bus packet size ( generally 4k max for page size )
#define BOOT_WAIT_BOOTLOADER_TIME 250
#define BOOT_WAIT_APPSTART_TIME 250


/** Target boot core */
enum EBootTargetCore
{
    EBootTargetCore_App,
    EBootTargetCore_VDP1,
    EBootTargetCore_VDP2,
    EBootTargetCore_APU,
    EBootTargetCore_MAX,
};


/** UF2 header */
typedef struct UF2DataBlock_t
{    
    uint32_t magicStart0;
    uint32_t magicStart1;
    uint32_t flags;
    uint32_t targetAddr;
    uint32_t payloadSize;
    uint32_t blockNo;
    uint32_t numBlocks;
    uint32_t fileSize; 
    uint8_t data[476];
    uint32_t magicEnd;
} UF2DataBlock_t;


/** Boot server options */
typedef struct BootServerOptions_t
{
    uint8_t coreId;

    // Forward link ( if any )
    BusTx_t* forwardBusTx;        // this -> next (out) 
    BusRx_t* forwardBusRx;        // this -> next (in) 

    // Link to app or parent core in chain eg. vdp1
    BusTx_t* appTx;        // this -> app (out) 
    BusRx_t* appRx;        // this -> app (in)     

} BootServerOptions_t;


/** Boot server instance */
typedef struct BootServer_t
{
    BusTx_t* forwardBusTx;
    BusRx_t* forwardBusRx;
    BusTx_t* appTx;
    BusRx_t* appRx;

    uint8_t coreId;
    bool running;
    bool lockCore;
} BootServer_t;


/** Boot client */
typedef struct BootClient_t
{
    struct BusTx_t busTx;
    struct BusRx_t busRx;    
} BootClient_t;


/** Boot options */
typedef struct BootImageInfo_t
{
    // UF2 boot image 
    bool bootImage;
    const uint8_t* uf2Bytes;
    const uint8_t* uf2Size;
    bool uf2Compressed;
} BootImageInfo_t;


// boot server apu
int bootserver_init(struct BootServer_t* server, struct BootServerOptions_t* options);  // init server
int bootserver_wait_with_timeout( struct BootServer_t* server, uint32_t timeout ); // run server loop with timeout
int bootserver_boot();

// boot client
int bootclient_init(struct BootClient_t* client, enum EBootTargetCore core, bool lockCore); // init client and fire async cmd to lock core
int bootclient_getStatus( struct BootClient_t* client, struct Cmd_Boot_GetStatus* statusOut, bool forwarded );  // get core status
int bootclient_write_uf2(struct BootClient_t* client, const uint8_t* bufffer, uint32_t sz, bool forwarded );      // write uf2 image to flash
int bootclient_erase_block(struct BootClient_t* client, uint32_t xipOffset, uint32_t sz, bool forwarded );    // erase block
int bootclient_write_block(struct BootClient_t* client, uint32_t xipOffset, struct Cmd_Boot_WriteFlashBlock* block, bool forwarded );    // write aligned block to flash ( 256 byte align )
int bootclient_read_block(struct BootClient_t* client, uint32_t xipOffset, struct Res_Boot_ReadFlashBlock* block, uint32_t sz, bool forwarded );    // read block from flash
int bootclient_boot(struct BootClient_t* client, bool forwarded);

// utils
int uf2_init(struct UF2DataBlock_t* uf2);
