#pragma once

#include "picocom/platform.h"
#include "lib/components/mock_hardware/queue.h"
#ifdef PICOCOM_NATIVE_SIM    

#else
    #include <pthread.h>    
#endif 

// Fwd
struct BusRx_t;
struct Cmd_Header_t;

#ifdef PICOCOM_NATIVE_SIM    
typedef void (*BlockingBusMsgHandler_t)(struct BusRx_t* bus, struct Cmd_Header_t* frame);
#endif


/* [dummy] */
enum pio_src_dest {
    pio_pins = 0u,
    pio_x = 1u,
    pio_y = 2u,
    pio_null = 3u,
    pio_pindirs = 4u,
    pio_exec_mov = 5u,
    pio_status = 6u,
    pio_pc = 7u,
    pio_isr = 8u,
    pio_osr = 9u,
    pio_exec_out = 10u,
};


/** PIO bind type */
enum EPIOBindType
{
    EPIOBindType_None,
    EPIOBindType_BusTx_to_Rx,
};


/** PIO simulation */
typedef struct PIO_t {
    const char* name;
    enum EPIOBindType bindType;    // created bind type
    void* userDataRx;   // bound rx bus
    void* userDataTx;   // bound tx bus    
#ifdef PICOCOM_NATIVE_SIM        
    struct BusRx_t* blockingBusRx;
    BlockingBusMsgHandler_t blockingRxHandler;    // Non-thread rx handler
#else        
    queue_t tx_out_queue;   // tx out -> to rx ( BusTx_t adds to queue)
    queue_t rx_ack_out_queue;   // rx ack event queue
#endif    
} PIO_t;

typedef struct PIO_t* PIO;


// mock api
PIO pio_mock_create();
