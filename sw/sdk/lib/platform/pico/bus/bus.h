#pragma once

#include "picocom/platform.h"
#include "stdint.h"
#include <stddef.h>
#include <stdint.h>
#ifdef PICOCOM_SDL
    #include "lib/components/mock_hardware/pio.h"
    #include "lib/components/mock_hardware/queue.h"
    #include "lib/components/mock_hardware/mutex.h"
#else
    #include "hardware/pio.h"
    #include "hardware/uart.h"
    #include "hardware/dma.h"
    #include "pico/util/queue.h"
    #include "pico/mutex.h"
#endif
#ifdef __cplusplus
extern "C" {
#endif


// Bus constants
#define BUS_MAX_PACKET_DMA_SIZE 1024*32                // Default config size (can override in config)
#define BUS_TX_PIO_END_INSTRUCTION_INDEX    9       // Index of PIO instruction of end loop (last instruction of bus_tx.pio, TODO: use a label for this)
#define BUS_DIV_SPI 3.0                             // stable 1.265958 MB/s
#define BUS_DIV_QPI 3.0                             // stable 1.812675 MB/s
#define BUS_DIV_OPI 2.0                             // stable 2.067895 MB/s NOTE: sw test limiting, should be faster
#define BUS_TX_RESPONSE_MAX_QUEUE  8
#define BUS_TX_REQUEST_MAX_QUEUE  8
#define BUS_RX_DMA_POOL_CNT 3


// Fwd
struct BusRx_t;
struct BusTx_t;
struct Cmd_Header_t;


// Bus callbacks
typedef void (*BusMsgHandler_t)(struct BusRx_t* bus, struct Cmd_Header_t* frame);
typedef void (*BusMsgAckHandler_t)(struct BusTx_t* bus, struct Cmd_Header_t* frame);
typedef void (*BlockingServiceCallHandler_t)(void* userData);


// Init command header helper
#define BUS_INIT_CMD(frameRef, cmdId) \
    frameRef.header.magic = EBusMagic_Header0; \
    frameRef.header.cmd = cmdId; \
    frameRef.header.id = 0; \
    frameRef.header.status = 0; \
    frameRef.header.flags = 0; \
    frameRef.header.crc = 0; \
    frameRef.header.sz = sizeof(frameRef); \
    frameRef.header.reserved = 0; \


#define BUS_INIT_CMD_PTR(frameRef, cmdId) \
    (frameRef)->header.magic = EBusMagic_Header0; \
    (frameRef)->header.cmd = cmdId; \
    (frameRef)->header.id = 0; \
    (frameRef)->header.status = 0; \
    (frameRef)->header.flags = 0; \
    (frameRef)->header.crc = 0; \
    (frameRef)->header.sz = sizeof(*frameRef); \
    (frameRef)->header.reserved = 0; \


#define BUS_INIT_RES(frameRef, cmdId, headerId) \
    BUS_INIT_CMD(frameRef, cmdId) \
    frameRef.header.id = headerId; \


/** Bus tx hw state
*/
typedef struct BusTx_t
{
    const char* name;
    int tx_pin_base;
    int tx_ack_pin;
    PIO tx_pio;
    queue_t tx_responseQueue;
    queue_t tx_requestQueue;    
    uint32_t tx_sm;
    int tx_irq;
    float tx_div;
    int tx_offset;
    uint32_t tx_byteloop_offset;
    uint32_t tx_idle_disarmed_offset;
    uint32_t tx_buswidth;    
    int tx_dma_chan;
    bool tx_dma_done;
    uint32_t tx_total_tx_bytes;
    uint32_t tx_total_send_cmd_cnt;
    uint8_t tx_rpc_id;
    uint32_t rx_ack_cnt;
    uint32_t rx_ack_cnt_inc_time;
    uint32_t rx_pending_ack_cnt;
    uint32_t rx_pending_ack_inc_time;    
    uint32_t tx_ack_timeout;
    uint32_t tx_ack_timeout_cnt;
    uint32_t tx_rpc_timeout_cnt;
    BusMsgAckHandler_t ack_handler;
    BusMsgAckHandler_t next_ack_handler;
    uint8_t* last_write_buffer; 
    uint32_t max_tx_size;
    uint16_t lastSeqNum;
    // stats
    uint32_t last_total_tx_bytes;
    uint32_t last_total_tx_time;
    uint32_t queue_request_main_overflow;
    uint32_t queue_response_main_overflow;
    int tx_debug;
    void* userData;
    mutex_t lock;
} BusTx_t;


/** Bus rx hw state
*/
typedef struct BusRx_t
{
    const char* name;
    int rx_pin_base;
    int rx_ack_pin;
    PIO rx_pio;
    uint32_t rx_sm;
    int rx_offset;
    int rx_offset_start;
    uint32_t rx_buswidth;        
    uint8_t* rx_buffer;
    uint32_t rx_buffer_size;
    uint32_t rx_dma_chan;
    int rx_wait_cnt;
    uint32_t rx_last_counter;
    uint32_t rx_success_cnt;
    uint32_t rx_total_rx_bytes;          
    BusMsgHandler_t rx_irq_handler;    
    BusMsgHandler_t rx_main_handler;    
    uint32_t rx_invalidHeaderCnt;    
    uint32_t rx_pendingCmdNotProcessedErrCnt;
    volatile bool rx_in_irq;
    volatile uint8_t* rx_pending_buffer;
    volatile uint32_t rx_pending_time;
    volatile uint32_t rx_response_cnt;
    volatile uint32_t rx_defer_cnt;    
    // stats
    uint32_t last_total_rx_bytes;
    uint32_t last_total_rx_time;    
    uint32_t rx_ack_cnt;
    void* userData;
    bool dispatch_ack_defer_handled;        // bus_rx_dispatch_main_cmd state for manual rx ack signaling
} BusRx_t;


/* Frame magic numbers*/
enum EBusMagic
{
    EBusMagic_Header0 = 0x4242    
};


/** Bus commands
 */
enum EBusCmd
{
    EBusCmd_NOP = 0,
    EBusCmd_BOOT = 8,
    EBusCmd_VDP2_BASE = 32,
    EBusCmd_VDP1_BASE = 64,
    EBusCmd_APU_BASE = 128,
    EBusCmd_APP_BASE = 192,
    EBusCmd_Max = 224
};


/** Cmd header flag */
enum ECmdHeaderFlags
{
    ECmdHeader_None,
    ECmdHeader_SendResponse = (1 << 0),    // Flagged as send response
};


/** Response cmds
 */
enum EBusResponse
{
    EBusResponse_NOP = 0,    
    EBusResponse_StatusResult = 1,
    EBusResponse_GetDiscReadData = 2,
};


/** Status flags */
enum EBusStatusFlags
{
    EBusStatusFlags_None,
    // targets
    EBusStatusFlags_User0 = (1 << 1),             
    EBusStatusFlags_User1 = (1 << 2),             
    EBusStatusFlags_User2 = (1 << 3),             
    EBusStatusFlags_User3 = (1 << 4),             
    // TX Host bits
    EBusStatusFlags_HostQueueSent = (1 << 5),       // Internal tx sent to dma queue ( assume sent )
    EBusStatusFlags_HostInQueue = (1 << 6),         // Internal tx in queue    
    EBusStatusFlags_NoCRC = (1 << 7),               // No crc
    // Masks
    EBusStatusFlags_TargetBits = (0b11110000),  
};


/** Low level header, every bus transaction starts with this header */
typedef struct __attribute__((__packed__)) Cmd_Header_t
{
    uint16_t magic;
    uint16_t seqNum;
    uint8_t id;
    uint16_t sz;    
    uint8_t cmd;      
    uint8_t status;  
    uint8_t flags;  
    uint16_t crc;
    uint16_t reserved;
} Cmd_Header_t;


/** Generic byte response */
typedef struct __attribute__((__packed__)) Res_uint8
{
    Cmd_Header_t header;        
    uint8_t result;
} Res_uint8;


/** Generic int response */
typedef struct __attribute__((__packed__)) Res_uint32
{
    Cmd_Header_t header;        
    uint32_t result;
} Res_uint32;


/** Generic int response */
typedef struct __attribute__((__packed__)) Res_int32
{
    Cmd_Header_t header;        
    int32_t result;
} Res_int32;


/** Generic data block */
typedef struct __attribute__((__packed__)) Cmd_DataBlock
{
    Cmd_Header_t header;            
    uint16_t size;
    uint8_t* data;
} Cmd_DataBlock;


/** Bus diagnostic stats, info on bus state */
typedef struct __attribute__((__packed__)) Res_Bus_Diag_Stats
{
    Cmd_Header_t header;        
    uint32_t totalBytesTx;  // Bus total bytes
    uint32_t totalBytesRx;
    uint32_t busErrorsTx;   // Errors
    uint32_t busErrorsRx; 
    float busRateTx;        // bus activity
    float busRateRx;    
} Res_Bus_Diag_Stas;



// bus rx api
uint16_t bus_calc_msg_crc(Cmd_Header_t* header);  // Calc crc field for message
void reset_pico_gpio();     // default gpio state
void bus_rx_configure(BusRx_t* bus,
        PIO rx_pio,
        uint32_t rx_sm,
        uint32_t rx_buswidth,
        int rx_pin_base,
        int rx_ack_pin
    );
void bus_rx_init(BusRx_t* bus);
void bus_rx_set_callback(BusRx_t* bus, BusMsgHandler_t irq_handler, BusMsgHandler_t main_handler); // set handler for fast irq and queued main
void bus_rx_push_defer_cmd(BusRx_t* bus, Cmd_Header_t* cmd);    // defer command from dispatch handler
Cmd_Header_t* bus_rx_get_next_deferred_cmd(BusRx_t* bus);       // Get next deferred command
void bus_rx_ack_deferred_cmd(BusRx_t* bus, Cmd_Header_t* cmd);  // Mark deferred as completed, ack response as processed
void bus_rx_dispatch_main_cmd(BusRx_t* bus, Cmd_Header_t* cmd); // Dispatch command on main
void bus_rx_update_stats(BusRx_t* bus_tx, struct Res_Bus_Diag_Stats* stats); // Add diag stats
void bus_rx_update(struct BusRx_t* busRx);

// bus tx api
void bus_tx_configure(BusTx_t* bus,        
        PIO tx_pio,
        uint32_t tx_sm,
        uint32_t tx_buswidth,
        int tx_pin_base,
        int tx_ack_pin,
        int tx_irq,
        float div
    );
void bus_tx_init(BusTx_t* bus);    
void bus_tx_reset(BusTx_t* bus);    
void bus_tx_set_debugger(BusTx_t* bus); // disable tx timeout for debugging
void bus_tx_write_async(BusTx_t* bus, uint8_t* buffer, int sz);
void bus_tx_write_cmd_async(BusTx_t* bus, Cmd_Header_t* frameOut);
void bus_tx_wait(BusTx_t* bus);
bool bus_tx_is_busy(BusTx_t* bus);
int bus_tx_flush(BusTx_t* bus);     // flush tx queue
int bus_tx_flush_one(BusTx_t* bus); // flush tx queue
int bus_tx_request_blocking(BusTx_t* bus_tx, BusRx_t* bus_rx, Cmd_Header_t* frameOut, Cmd_Header_t* frameResponse, size_t responseSize, uint32_t timeoutMs);
int bus_tx_request_blocking_ex(BusTx_t* bus_tx, BusRx_t* bus_rx, Cmd_Header_t* frameOut, Cmd_Header_t* frameResponse, size_t responseSize, uint32_t timeoutMs, BlockingServiceCallHandler_t handler, void* userData);
bool bus_tx_queue_request_from_irq(BusTx_t* bus, Cmd_Header_t* frameOut);   // Queue response from irq 
bool bus_tx_queue_request_from_main(BusTx_t* bus, Cmd_Header_t* frameOut); // Queue request from app ( ensures fair processing of irq )
int bus_tx_queue_get_level_main(BusTx_t* bus);
int bus_tx_queue_get_level_irq(BusTx_t* bus);
bool bus_tx_is_queueResponse_sent(BusTx_t* bus, Cmd_Header_t* frameOut);    // Checks status of bus_tx_queueResponse send, uses header.size==0 as method to detect this state, queue writer complete will zero command
bool bus_tx_is_queueResponse_waiting(BusTx_t* bus, Cmd_Header_t* frameOut); // Check status bit for waiting in queue
bool bus_tx_can_send(BusTx_t* bus, Cmd_Header_t* frameOut);                 // Check if status bit can send next
int bus_tx_update(BusTx_t* bus); // tick bush and process pending async requests
void bus_tx_pulse_debug_outputs(BusTx_t* bus_tx);
void bus_tx_update_stats(BusTx_t* bus_tx, struct Res_Bus_Diag_Stats* stats); // Add diag stats, returns true if rates updated
int bus_tx_rpc_set_return_irq(BusTx_t* bus_tx, Cmd_Header_t* reqFrameOut, Cmd_Header_t* frameOut); // Return value for rpc, pass original request ref to set its response value
int bus_tx_rpc_set_return_main(BusTx_t* bus_tx, Cmd_Header_t* reqFrameOut, Cmd_Header_t* frameOut); // [depreciated] Return value for rpc, pass original request ref to set its response value, BUGGED: blocks if queue full as cant flush with pending rx ack
void bus_tx_set_ack_callback(BusTx_t* bus, BusMsgAckHandler_t ack_handler); // global set ack handler to schedule next tx commands globayl
void bus_tx_next_ack_callback(BusTx_t* bus, BusMsgAckHandler_t ack_handler); // next ack handler to schedule next tx command (allows contious streaming), clears on ack & overrides global
void bus_debug_print_frame(Cmd_Header_t* frameOut);

// Testing
int bus_txrx_rpc_set_return_main(BusTx_t* bus_tx, BusRx_t* bus_rx, Cmd_Header_t* reqFrameOut, Cmd_Header_t* frameOut); // issues with bus_tx_rpc_set_return_main blocking, this was added but doesnt solve anything

#ifdef __cplusplus
}
#endif
