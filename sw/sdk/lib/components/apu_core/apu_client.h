#pragma once

#include "picocom/platform.h"
#include "platform/pico/bus/bus.h"
#include "platform/pico/apu/hw_apu_types.h"


/** APU client init options */
typedef struct ApuClientInitOptions_t {
    struct BusTx_t* apuLink_tx;
    struct BusRx_t* apuLink_rx;            
} ApuClientInitOptions_t;


/** APU client */
typedef struct ApuClientImpl_t
{
    struct BusTx_t* apuLink_tx;
    struct BusRx_t* apuLink_rx;    
    uint32_t defaultTimeout;
    uint32_t maxCmdSize;                // max possible gpu cmd       
    bool lastHasStorage; 
} ApuClientImpl_t;


// APU client api
struct ApuClientImpl_t* apu_client_init(struct ApuClientInitOptions_t* options);