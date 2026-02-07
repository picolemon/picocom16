#include "picocom/devkit.h"
#include "callback_bus.h"
#include "crc16/crc.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

bool bus_tx_is_done(BusTx_t* bus);

//
// mock router


int bus_mock_router_create(struct busMockRouter_t* router, int threaded)
{
    memset(router, 0, sizeof(struct busMockRouter_t));

    router->name = "mock_router";    
    router->running = true;
    heap_array_init(&router->pios, sizeof(PIO), 1024);        

    return 0;
}


int bus_mock_link_pio(struct busMockRouter_t* router, PIO* tx, PIO* rx )
{    
    // create tx -> rx thread
    PIO tx_to_rx_pio = pio_mock_create(); 
    *tx = tx_to_rx_pio;
    *rx = tx_to_rx_pio;
    tx_to_rx_pio->bindType = EPIOBindType_BusTx_to_Rx;

    return 1;
}


// 
// bus api


uint16_t bus_calc_msg_crc(Cmd_Header_t* header)
{	
	if(header->sz == 0)
		return 0;
	uint8_t* payload = (uint8_t*)header;
	payload += sizeof(Cmd_Header_t); // skip header
	return picocom_crc16( (const char*)payload, header->sz - sizeof(Cmd_Header_t));	
}


void bus_rx_configure(BusRx_t* bus,
        PIO rx_pio,
        uint32_t rx_sm,
        uint32_t rx_buswidth,
        int rx_pin_base,
        int rx_ack_pin
    )
{
    memset(bus, 0, sizeof(BusRx_t));
    bus->rx_pio = rx_pio;
    bus->rx_sm = rx_sm;
    bus->rx_buswidth = rx_buswidth;
    bus->rx_pin_base = rx_pin_base;
    bus->rx_ack_pin = rx_ack_pin;
    bus->name = "rx";
    bus->rx_buffer_size = BUS_MAX_PACKET_DMA_SIZE;
}


void bus_rx_init(BusRx_t* bus)
{
    // bind bus    
    if(bus->rx_pio)
        bus->rx_pio->userDataRx = bus;

    // alloc rx buffer    
    bus->rx_buffer = picocom_malloc(bus->rx_buffer_size);
    if(!bus->rx_buffer)
        picocom_panic(SDKErr_Fail, "Failed to alloc bus rx buffer");

}


void bus_rx_set_callback(BusRx_t* bus, BusMsgHandler_t irq_handler, BusMsgHandler_t main_handler)
{
    bus->rx_irq_handler = irq_handler;
    bus->rx_main_handler = main_handler;    

#ifdef PICOCOM_NATIVE_SIM 
    bus->rx_pio->blockingRxHandler = irq_handler;
    bus->rx_pio->blockingBusRx = bus;
#endif

}


void bus_rx_push_defer_cmd(BusRx_t* bus, Cmd_Header_t* cmd)
{
    // Mark as pending  
    bus->rx_pending_buffer = bus->rx_buffer;
    bus->rx_defer_cnt++;
}


Cmd_Header_t* bus_rx_get_next_deferred_cmd(BusRx_t* bus)
{
    Cmd_Header_t* cmd = (Cmd_Header_t*)bus->rx_pending_buffer;
    return cmd;
}

void bus_rx_dispatch_main_cmd(BusRx_t* bus, Cmd_Header_t* cmd)
{
    bus->dispatch_ack_defer_handled = false;

    if(bus->rx_main_handler)
        bus->rx_main_handler(bus, cmd);

    if(!bus->dispatch_ack_defer_handled)
        bus_rx_ack_deferred_cmd( bus, cmd );            
}


void bus_rx_update_stats(BusRx_t* bus_rx, struct Res_Bus_Diag_Stats* stats)
{
    stats->totalBytesRx = bus_rx->rx_total_rx_bytes;
    stats->busErrorsRx = bus_rx->rx_invalidHeaderCnt;
    stats->busErrorsRx += bus_rx->rx_pendingCmdNotProcessedErrCnt;      

    uint32_t t1 = picocom_time_us_32(); 
    double deltaS = (t1-bus_rx->last_total_rx_time) / 1000000.0f;
    int diff = bus_rx->rx_total_rx_bytes - bus_rx->last_total_rx_bytes;
    double byteSec = diff / deltaS;

    if(diff > 1.0)
    {
        bus_rx->last_total_rx_bytes = bus_rx->rx_total_rx_bytes;            
        bus_rx->last_total_rx_time = picocom_time_us_32(); 
    }

    stats->busRateRx = byteSec;      
}


// bus tx api
void bus_tx_configure(BusTx_t* bus,        
        PIO tx_pio,
        uint32_t tx_sm,
        uint32_t tx_buswidth,
        int tx_pin_base,
        int tx_ack_pin,
        int tx_irq,
        float div
    )
{
    memset(bus, 0, sizeof(BusTx_t));

    bus->tx_pio = tx_pio;
    bus->tx_sm = tx_sm;
    bus->tx_buswidth = tx_buswidth;
    bus->tx_pin_base = tx_pin_base;
    bus->tx_ack_pin = tx_ack_pin;
    bus->tx_irq = tx_irq;
    bus->tx_div = div;
    bus->tx_ack_timeout = 0;
    bus->max_tx_size = BUS_MAX_PACKET_DMA_SIZE;
    bus->name = "tx";
}

void bus_tx_init(BusTx_t* bus)
{
    queue_init(&bus->tx_responseQueue, sizeof(Cmd_Header_t*), BUS_TX_RESPONSE_MAX_QUEUE);
    queue_init(&bus->tx_requestQueue, sizeof(Cmd_Header_t*), BUS_TX_REQUEST_MAX_QUEUE);
    
    // bind bus    
    if(bus->tx_pio)
        bus->tx_pio->userDataTx = bus;  
}


void bus_tx_set_debugger(BusTx_t* bus)
{
    bus->tx_ack_timeout = 0xffffffff;
}


void bus_tx_write_async(BusTx_t* bus, uint8_t* buffer, int sz)
{
    // should always be header otherwise rx would never work
    bus->lastSeqNum++;

    struct Cmd_Header_t* frame = (struct Cmd_Header_t*)buffer;
    frame->seqNum = bus->lastSeqNum;
    //printf("\t[%s] bus_tx_write_async buffer:0x%" PRIXPTR ", cmd: %d,  sz: %d (ackstate:%d/%d)\n", bus->name, (long unsigned int)buffer, frame->cmd, sz, bus->rx_pending_ack_cnt, bus->rx_ack_cnt);
    
    if(sz > bus->max_tx_size)
        picocom_panic(SDKErr_Fail, "max packet size");

    // ensure not busy, block until dma completion
    while (!bus_tx_is_done(bus)) 
    {        
        tight_loop_contents();        
        picocom_sleep_us(0);

        bus_tick_cores_hack();
    }

    // Ensure wait was called
    if(bus->rx_pending_ack_cnt != bus->rx_ack_cnt)
    {
        int wasBusy = bus_tx_is_busy(bus);
        int wasDone = bus_tx_is_done(bus);
        picocom_panic(SDKErr_Fail, "writing to bus with pending ack");
    }

    // next ack
    bus->rx_pending_ack_cnt++;
    bus->rx_pending_ack_inc_time = picocom_time_us_32();

    bus->last_write_buffer = buffer;

    if( bus->tx_pio->blockingRxHandler )
    {
        if( sz <= bus->tx_pio->blockingBusRx->rx_buffer_size )
        {
            // Copy to target rx buffer
            memcpy( bus->tx_pio->blockingBusRx->rx_buffer, buffer, sz );
            bus->tx_pio->blockingRxHandler( bus->tx_pio->blockingBusRx, (Cmd_Header_t*)bus->tx_pio->blockingBusRx->rx_buffer );    

            // Check if was not deferred
            if(!bus->tx_pio->blockingBusRx->rx_pending_buffer)
            {                
                bus->rx_ack_cnt++; // fake ack
            }
        }
    }
    else
    {
        picocom_panic(SDKErr_Fail, "!blockingRxHandler");
    }
    
    // stat
    bus->tx_total_tx_bytes += sz;  
    bus->tx_total_send_cmd_cnt++;
}


void bus_tx_write_cmd_async(BusTx_t* bus, Cmd_Header_t* frameOut)
{
    // set sent flag
    frameOut->status |= EBusStatusFlags_HostQueueSent;
    // clear waiting bit
    frameOut->status &= ~(EBusStatusFlags_HostInQueue);

    bus_tx_write_async(bus, (uint8_t*)frameOut, frameOut->sz);
}



bool bus_tx_is_done(BusTx_t* bus)
{    
    if(bus->rx_ack_cnt != bus->rx_pending_ack_cnt)
    {
        if(bus->tx_ack_timeout != 0 && picocom_time_us_32() - bus->rx_pending_ack_inc_time > bus->tx_ack_timeout)
        {
            bus->rx_ack_cnt = bus->rx_pending_ack_cnt;
            bus->tx_ack_timeout_cnt++;
        }
    }
    
    return bus->rx_ack_cnt == bus->rx_pending_ack_cnt;
}



void bus_tx_wait(BusTx_t* bus)
{
#ifdef PICOCOM_NATIVE_SIM    
    return;
#else    
    uint32_t start = picocom_time_us_32();    
    uint32_t lastLogTime = start;

    while (!bus_tx_is_done(bus)) 
    {        
        tight_loop_contents();        
        picocom_sleep_us(0);

        uint32_t now = picocom_time_us_32();  
        if( now - start > 100000 )
        {            
            if( now - lastLogTime > 100000 )
            {
                lastLogTime =  now;
                //printf("\t[%s] bus_tx_is_done[timeout] dt: %d\n", bus->name, (int)(now - start) );
            }
        }
    }
#endif    
}


bool bus_tx_is_busy(BusTx_t* bus)
{
    return !bus_tx_is_done(bus);
}


int bus_tx_flush(BusTx_t* bus)
{
    int result = 0;    
    while(queue_get_level(&bus->tx_responseQueue) || queue_get_level(&bus->tx_requestQueue))
    {
        result += bus_tx_flush_one(bus); // flush tx queue       
    }

    return result;
}


int bus_tx_flush_one(BusTx_t* bus)
{
    int result = 0;
    Cmd_Header_t* nextCmd;

    while(bus_tx_is_busy(bus))
    {
        tight_loop_contents();
    }

    // interleave irq & app req
    if(queue_try_remove(&bus->tx_responseQueue, &nextCmd)) 
    {        
        bus_tx_write_cmd_async(bus, nextCmd);
        result++;      
    }
    if(queue_try_remove(&bus->tx_requestQueue, &nextCmd)) 
    {        
        bus_tx_write_cmd_async(bus, nextCmd);
        result++;      
    } 

    return result;
}

#ifndef PICOCOM_NATIVE_SIM
int bus_tx_request_blocking(BusTx_t* bus_tx, BusRx_t* bus_rx, Cmd_Header_t* frameOut, Cmd_Header_t* frameResponse, size_t responseSize, uint32_t timeoutMs)
{
    return bus_tx_request_blocking_ex( bus_tx, bus_rx, frameOut, frameResponse, responseSize, timeoutMs, 0, 0 );
}
#endif


int bus_tx_request_blocking_ex(BusTx_t* bus_tx, BusRx_t* bus_rx, Cmd_Header_t* frameOut, Cmd_Header_t* frameResponse, size_t responseSize, uint32_t timeoutMs, BlockingServiceCallHandler_t handler, void* userData)
{
    uint32_t timeoutUs = timeoutMs * 1000;

    bus_tx_flush(bus_tx); // flush bus before sending more

    // mark last rx count for result polling
    volatile int rx_response_cnt = bus_rx->rx_response_cnt;
    volatile int rx_defer_cnt = bus_rx->rx_defer_cnt;

    // assign unique rpc id
    uint8_t tx_rpc_id = bus_tx->tx_rpc_id++;
    frameOut->id = tx_rpc_id;    
    if(!(frameOut->status & EBusStatusFlags_NoCRC))
        frameOut->crc = bus_calc_msg_crc(frameOut);

    // write command
    bus_tx_queue_request_from_main(bus_tx, frameOut);
    bus_tx_update(bus_tx);

    uint32_t startTime = picocom_time_us_32();

    // wait loop
    while(1)
    {    
        bus_tx_update(bus_tx);
        picocom_wdt();

        if(handler)
            handler(userData);
            
        Cmd_Header_t* frame = bus_rx_get_next_deferred_cmd( bus_rx);
        if( frame ) 
        {
            if(bus_rx->rx_main_handler)
                bus_rx->rx_main_handler(bus_rx, frame);

            if(frame->cmd == frameOut->cmd)
            {
                rx_response_cnt = bus_rx->rx_response_cnt;
                rx_defer_cnt = bus_rx->rx_defer_cnt;

                // validate matches response
                int expectSize = responseSize;                
                if(frame->sz == expectSize && frameOut->id == tx_rpc_id)
                {   
                    // copy result
                    memcpy(frameResponse, frame, responseSize);

                    // ack deferred
                    bus_rx_ack_deferred_cmd(bus_rx, 0); 

                    // return ok
                    return SDKErr_OK;
                }
            }

            // ack completed work
            bus_rx_ack_deferred_cmd( bus_rx, 0 );    
        }

        if( picocom_time_us_32() - startTime > timeoutUs )
        {
            bus_tx->tx_rpc_timeout_cnt++;
            return SDKErr_Fail; // Timeout
        }
    }

    // cleanup
    return SDKErr_Fail;
}


bool bus_tx_queue_request_from_irq(BusTx_t* bus, Cmd_Header_t* frameOut)
{
    if(frameOut->sz >= bus->max_tx_size)
        picocom_panic(SDKErr_Fail, "max packet size");

    // clear sent bit
    frameOut->status &= ~(EBusStatusFlags_HostQueueSent);
    // Set waiting bit
    frameOut->status |= EBusStatusFlags_HostInQueue;

    if(!queue_try_add(&bus->tx_responseQueue, &frameOut))
    {
        bus->queue_response_main_overflow++;
        return false;
    }
    else
        return true;    
}


bool bus_tx_queue_request_from_main(BusTx_t* bus, Cmd_Header_t* frameOut)
{
    if(frameOut->sz >= bus->max_tx_size)
        picocom_panic(SDKErr_Fail, "max packet size");

    // clear sent bit
    frameOut->status &= ~(EBusStatusFlags_HostQueueSent);
    // Set waiting bit
    frameOut->status |= EBusStatusFlags_HostInQueue;
    if(!queue_try_add(&bus->tx_requestQueue, &frameOut))
    {
        bus->queue_request_main_overflow++;
        return false;
    }
    else
        return true;
}


int bus_tx_queue_get_level_main(BusTx_t* bus)
{
    return queue_get_level(&bus->tx_requestQueue);
}


int bus_tx_queue_get_level_irq(BusTx_t* bus)
{
    return queue_get_level(&bus->tx_responseQueue);
}


bool bus_tx_is_queueResponse_sent(BusTx_t* bus, Cmd_Header_t* frameOut)
{
    // Check tx status
    return frameOut->status & EBusStatusFlags_HostQueueSent;
}


bool bus_tx_is_queueResponse_waiting(BusTx_t* bus, Cmd_Header_t* frameOut)
{
    // Check tx status
    return frameOut->status & EBusStatusFlags_HostInQueue;
}


bool bus_tx_can_send(BusTx_t* bus, Cmd_Header_t* frameOut)
{
    if(bus_tx_is_queueResponse_sent(bus, frameOut) || !bus_tx_is_queueResponse_waiting(bus, frameOut))
        return true;
    return false;
}



int bus_tx_update(BusTx_t* bus)
{
      int result = 0;
    Cmd_Header_t* nextCmd;
    while(queue_get_level(&bus->tx_responseQueue) || queue_get_level(&bus->tx_requestQueue))
    {
        // break when busy to keep things async
        if(bus_tx_is_busy(bus))
            break;

        if(bus->tx_debug)
            bus->tx_debug = bus->tx_debug;

        // interleave irq & app req        
        if(queue_try_remove(&bus->tx_responseQueue, &nextCmd)) 
        {        
            bus_tx_write_cmd_async(bus, nextCmd);
            result++;      
        }

        // break when busy to keep things async
        if(bus_tx_is_busy(bus))
            break;

        if(bus->tx_debug)
            bus->tx_debug = bus->tx_debug;

        if(queue_try_remove(&bus->tx_requestQueue, &nextCmd)) 
        {        
            bus_tx_write_cmd_async(bus, nextCmd);
            result++;      
        }        
    }

    return result;
}


int bus_tx_rpc_set_return_irq(BusTx_t* bus_tx, Cmd_Header_t* reqFrameOut, Cmd_Header_t* frameOut)
{
    // Link required to match request on sender end
    frameOut->cmd = reqFrameOut->cmd;
    frameOut->id = reqFrameOut->id;

    // Queue
    return bus_tx_queue_request_from_irq(bus_tx, frameOut);    
}


int bus_tx_rpc_set_return_main(BusTx_t* bus_tx, Cmd_Header_t* reqFrameOut, Cmd_Header_t* frameOut)
{
    // Link required to match request on sender end
    frameOut->cmd = reqFrameOut->cmd;
    frameOut->id = reqFrameOut->id;

    // Ensure room
    if(bus_tx_queue_get_level_main(bus_tx) > BUS_TX_REQUEST_MAX_QUEUE / 2)
        bus_tx_flush_one(bus_tx);

    // Queue
    return bus_tx_queue_request_from_main(bus_tx, frameOut);    
}


// 
// bus testing

void bus_rx_print_stats(const char* busname, BusRx_t* bus_rx)
{
    struct Res_Bus_Diag_Stats stats = {};
    bus_rx_update_stats(bus_rx, &stats); 
    printf("[%s] rx(%f b/s, %f MB/s), scnt:%d, enproc:%d, ehdr: %d%s, ackcnt: %d\n", busname, stats.busRateRx, stats.busRateRx/1000000.0f, 
        bus_rx->rx_success_cnt,
        bus_rx->rx_pendingCmdNotProcessedErrCnt, bus_rx->rx_invalidHeaderCnt, bus_rx->rx_in_irq ? "*":"", bus_rx->rx_ack_cnt);
}


bool check_interval( uint32_t* lastTimePtr, float inverval )
{
    uint32_t now = picocom_time_us_32(); 
    uint32_t lastTime = *lastTimePtr;

    float deltaSec = (now-lastTime) / 1000000.0f;
    if( deltaSec > inverval )
    {
        *lastTimePtr = now;
        return true;
    }
    else 
    {
        return false;
    }    
}


void bus_tx_print_stats(const char* busname, BusTx_t* bus_tx)
{
    struct Res_Bus_Diag_Stats stats = {};
    bus_tx_update_stats(bus_tx, &stats); 
    printf("[%s] tx(%f b/s, %f MB/s), ak(%d->%d) txcnt:%d, ack_cnt:%d, err:%d\n", 
        busname, 
        stats.busRateTx, 
        stats.busRateTx/1000000.0f,
        bus_tx->rx_pending_ack_cnt, 
        bus_tx->rx_ack_cnt,
        bus_tx->tx_total_send_cmd_cnt, 
        bus_tx->rx_ack_cnt,
        stats.busErrorsTx
    );
}


void bus_tx_update_stats(BusTx_t* bus_tx, struct Res_Bus_Diag_Stats* stats)
{
    stats->totalBytesTx = bus_tx->tx_total_tx_bytes;

    stats->busErrorsTx = bus_tx->tx_ack_timeout_cnt;
    stats->busErrorsTx += bus_tx->tx_rpc_timeout_cnt;
    stats->busErrorsTx += bus_tx->queue_request_main_overflow;
    stats->busErrorsTx += bus_tx->queue_response_main_overflow;

    uint32_t t1 = picocom_time_us_32(); 
    double deltaS = (t1-bus_tx->last_total_tx_time) / 1000000.0f;
    int diff = bus_tx->tx_total_tx_bytes - bus_tx->last_total_tx_bytes;
    double byteSec = diff / deltaS;

    // sample every 1s
    if(diff > 1.0)
    {
        bus_tx->last_total_tx_bytes = bus_tx->tx_total_tx_bytes;            
        bus_tx->last_total_tx_time = picocom_time_us_32(); 
    }

    stats->busRateTx = byteSec; 
}

void bus_rx_update(struct BusRx_t* busRx)
{
    Cmd_Header_t* frame = bus_rx_get_next_deferred_cmd( busRx );
    if( frame ) 
    {
        busRx->rx_pending_buffer = 0; // Prevent recall, bus_rx_ack_deferred_cmd normally clear this but sub tick might re-trigger
        bus_rx_dispatch_main_cmd(busRx, frame);
    }
}


void bus_rx_ack_deferred_cmd(BusRx_t* bus, Cmd_Header_t* cmd)
{
    bus->rx_ack_cnt++;
    bus->rx_pending_buffer = 0;

    // Trigger ack on remote
    BusTx_t* ackBus = bus->rx_pio->userDataTx;
    if(ackBus)
        ackBus->rx_ack_cnt++;    
}


void bus_tx_reset(struct BusTx_t* busRx)
{
    
}


//
//
int core_manager_create(struct coreManager_t* mgr)
{
    memset(mgr, 0, sizeof(struct coreManager_t));    
    return 1;
}


//
//
void test_core_vdp1_update(void* userData);
void test_core_vdp2_update();
void test_core_apu_full_update();

void bus_tick_cores_hack()
{
    // testing    
    test_core_vdp1_update(0);
    test_core_vdp2_update();
    test_core_apu_full_update();
}
