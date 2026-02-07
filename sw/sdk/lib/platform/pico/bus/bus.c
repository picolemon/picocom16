#pragma GCC optimize ("O0") // optim levels breaks bus timing on HW

#include <unistd.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <pico/mutex.h>
#include <pico/platform/panic.h>
#include <stdint.h>
#include "bus_tx.pio.h"
#include "bus_rx.pio.h"
#include "bus.h"
#include "bus_testing.h"
#include "pico/stdlib.h"
#include "crc16/crc.h"
#include "hardware/structs/bus_ctrl.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "picocom/platform.h"
#include "thirdparty/crc16/crc.h"


// Fwd
void picocom_wdt();


/** Cached program
*/
typedef struct BusProgramCache
{   
    bool cacheValid;
    int bus_tx_1b_offset;
    int bus_tx_2b_offset;
    int bus_tx_4b_offset;
    int bus_tx_8b_offset;

    int bus_rx_1b_offset;
    int bus_rx_2b_offset;
    int bus_rx_4b_offset;
    int bus_rx_8b_offset;    
} BusProgramCache;


// Globals
BusTx_t* g_bus_tx_pio_sm[16] = {}; // pio:sm -> bus map
BusTx_t* g_bus_tx_pio[3] = {}; // pio -> bus map
BusTx_t* g_bus_tx_gpio[32] = {};
BusRx_t* g_bus_rx_gpio[32] = {};
BusProgramCache g_pio_program_cache[3] = {};   // cache programs in each pio block
mutex_t g_pio_lock[3] = {};       // hw lock for each PIO


#define PIOSM_ID_TO_INDEX(pioIndex, sm) \
    ( ((sm & 0b11) << 2) | (pioIndex & 0xb11) )


static void prog_cache_init(int pio_index)
{
    if(!g_pio_program_cache[pio_index].cacheValid)
    {
        g_pio_program_cache[pio_index].cacheValid = 1;
        g_pio_program_cache[pio_index].bus_tx_1b_offset = -1;
        g_pio_program_cache[pio_index].bus_tx_2b_offset = -1;
        g_pio_program_cache[pio_index].bus_tx_4b_offset = -1;
        g_pio_program_cache[pio_index].bus_tx_8b_offset = -1;

        g_pio_program_cache[pio_index].bus_rx_1b_offset = -1;
        g_pio_program_cache[pio_index].bus_rx_2b_offset = -1;
        g_pio_program_cache[pio_index].bus_rx_4b_offset = -1;
        g_pio_program_cache[pio_index].bus_rx_8b_offset = -1;        
    }
}


uint16_t bus_calc_msg_crc(Cmd_Header_t* header)
{	
	if(header->sz == 0)
		return 0;
	uint8_t* payload = (uint8_t*)header;
	payload += sizeof(Cmd_Header_t); // skip header
	return picocom_crc16( (const char*)payload, header->sz - sizeof(Cmd_Header_t));	
}


//
// ===================================================
void rx_gpio_cs_callback(uint gpio, uint32_t events)
{
    // map gpio to bus
    BusRx_t* bus = g_bus_rx_gpio[gpio];
    if(bus)
    {
        bus->rx_in_irq = 1;

        // complete pio fifo xfer
        dma_channel_hw_t* hw = dma_channel_hw_addr(bus->rx_dma_chan);
        int transfer_count = bus->rx_buffer_size - hw->transfer_count;
        while (!pio_sm_is_rx_fifo_empty(bus->rx_pio, bus->rx_sm) 
            && (hw->transfer_count != 0)) 
        {
            bus->rx_wait_cnt++;
            tight_loop_contents();
        }

        static uint8_t lastValidCmd;

        // detect bad bus sync/handling of rx buffers. rx_pending_buffer should be null after handling but now contains overwritten data.
        // This can happen if the device became out of sync and a bus reset is needed.
        if( bus->rx_pending_buffer && (time_us_32()-bus->rx_pending_time) < 100000 )
        {
            bus->rx_pendingCmdNotProcessedErrCnt++;

            // debug hint, see lastValidCmd for last cmd and newCmd contains overwritten
            static Cmd_Header_t* newCmd;
            newCmd = (Cmd_Header_t*)bus->rx_pending_buffer;
            if( newCmd)
            {                                
                newCmd = newCmd;
            }            
        }
        else 
        {
            // total stat
            bus->rx_total_rx_bytes += transfer_count;
            
            // validate cmd
            Cmd_Header_t* cmd = (Cmd_Header_t*)bus->rx_buffer;
            lastValidCmd = cmd->cmd;
            if(cmd->magic == EBusMagic_Header0 && transfer_count >= sizeof(Cmd_Header_t) 
                && transfer_count <= bus->rx_buffer_size)
            {                   
                // dispatch
                if(bus->rx_irq_handler)
                {
                    bus->rx_irq_handler(bus, cmd);
                }
                else
                {
                    bus_rx_push_defer_cmd(bus, cmd); // default defer
                }

                // mark success
                bus->rx_success_cnt++;                           
                bus->rx_response_cnt++;  
            }
            else 
            {
                bus->rx_invalidHeaderCnt++;
            }
        }

        // fast rearm            
        dma_channel_abort(bus->rx_dma_chan);
        pio_sm_set_enabled(bus->rx_pio, bus->rx_sm, false);
        pio_sm_clear_fifos(bus->rx_pio, bus->rx_sm);
        pio_sm_restart(bus->rx_pio, bus->rx_sm);      
        pio_sm_exec_wait_blocking(bus->rx_pio, bus->rx_sm, pio_encode_jmp(bus->rx_offset_start));
        pio_sm_set_enabled(bus->rx_pio, bus->rx_sm, true);
        dma_channel_transfer_to_buffer_now(bus->rx_dma_chan, bus->rx_buffer,  bus->rx_buffer_size);       

        // Check if was not deferred
        if(!bus->rx_pending_buffer)
        {
            bus_rx_ack_deferred_cmd(bus, 0); 
        }

        bus->rx_in_irq = 0;
    }
    else 
    {
        BusTx_t* bus_tx = g_bus_tx_gpio[gpio];
        assert(bus_tx);
        bus_tx->rx_ack_cnt++;

        if(bus_tx->next_ack_handler && bus_tx->last_write_buffer)
        {   
            // Clear next
            BusMsgAckHandler_t handler = bus_tx->next_ack_handler;
            bus_tx->next_ack_handler = 0;

            // Fire single
            handler(bus_tx, (struct Cmd_Header_t* )bus_tx->last_write_buffer);
        }
        else if(bus_tx->ack_handler && bus_tx->last_write_buffer)
        {   
            bus_tx->ack_handler(bus_tx, (struct Cmd_Header_t* )bus_tx->last_write_buffer);
        }

        return;
    }
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
    uint32_t pio_index = pio_get_index(bus->rx_pio);    

    // alloc rx buffer    
    bus->rx_buffer = picocom_malloc(bus->rx_buffer_size);
    if(!bus->rx_buffer)
        panic("Failed to alloc bus rx buffer");

    // init cache
    prog_cache_init(pio_index);
    
    pio_sm_config c;
    switch (bus->rx_buswidth) 
    {
        case 1:
            if(g_pio_program_cache[pio_index].bus_rx_1b_offset == -1)
            {
               g_pio_program_cache[pio_index].bus_rx_1b_offset = pio_add_program(bus->rx_pio, &bus_rx_1b_program);
            }
            bus->rx_offset = g_pio_program_cache[pio_index].bus_rx_1b_offset;

            c = bus_rx_1b_program_get_default_config(bus->rx_offset);
            bus->rx_offset_start = bus->rx_offset + bus_rx_1b_offset_start;
            break;
        case 2:
            if(g_pio_program_cache[pio_index].bus_rx_2b_offset == -1)
            {
               g_pio_program_cache[pio_index].bus_rx_2b_offset = pio_add_program(bus->rx_pio, &bus_rx_2b_program);
            }
            bus->rx_offset = g_pio_program_cache[pio_index].bus_rx_2b_offset;

            c = bus_rx_2b_program_get_default_config(bus->rx_offset);
            bus->rx_offset_start = bus->rx_offset + bus_rx_2b_offset_start;
            break;            
        case 4:
            if(g_pio_program_cache[pio_index].bus_rx_4b_offset == -1)
            {
               g_pio_program_cache[pio_index].bus_rx_4b_offset = pio_add_program(bus->rx_pio, &bus_rx_4b_program);
            }
            bus->rx_offset = g_pio_program_cache[pio_index].bus_rx_4b_offset;

            c = bus_rx_4b_program_get_default_config(bus->rx_offset);
            bus->rx_offset_start = bus->rx_offset + bus_rx_4b_offset_start;
            break;                
        case 8:
            if(g_pio_program_cache[pio_index].bus_rx_8b_offset == -1)
            {
               g_pio_program_cache[pio_index].bus_rx_8b_offset = pio_add_program(bus->rx_pio, &bus_rx_8b_program);
            }
            bus->rx_offset = g_pio_program_cache[pio_index].bus_rx_8b_offset;

            c = bus_rx_8b_program_get_default_config(bus->rx_offset);
            bus->rx_offset_start = bus->rx_offset + bus_rx_8b_offset_start;
            break;                   
            
        default:
            panic("Invalid bus width");
    }        

    if(bus->rx_offset < 0)
    {
        panic("Failed to add PIO rx program");
    }
    
    pio_sm_set_consecutive_pindirs(bus->rx_pio, bus->rx_sm, bus->rx_pin_base, bus->rx_buswidth + 2, false);

    for(int i=0;i<bus->rx_buswidth + 2;i++) // 8 bits + clk,cs    
    {
        gpio_init( bus->rx_pin_base + i );
        gpio_set_dir( bus->rx_pin_base + i, false );
    }
    
    sm_config_set_in_pins(&c, bus->rx_pin_base);         
    sm_config_set_in_shift(&c, false, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);                
    sm_config_set_clkdiv(&c, 1.0);
    
    pio_sm_init( bus->rx_pio, bus->rx_sm, bus->rx_offset, &c );

    // RX DMA setup
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // alloc dma    
    bus->rx_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config dmaConf;
    dmaConf = dma_channel_get_default_config(bus->rx_dma_chan);
    channel_config_set_transfer_data_size(&dmaConf, DMA_SIZE_8);
    channel_config_set_read_increment(&dmaConf, false);
    channel_config_set_write_increment(&dmaConf, true);
    channel_config_set_dreq(&dmaConf, pio_get_dreq(bus->rx_pio, bus->rx_sm, false));
    dma_channel_set_read_addr(bus->rx_dma_chan, &bus->rx_pio->rxf[bus->rx_sm], false);
    dma_channel_set_write_addr(bus->rx_dma_chan, bus->rx_buffer, false);
    dma_channel_set_trans_count(bus->rx_dma_chan, bus->rx_buffer_size, false);
    dma_channel_set_config(bus->rx_dma_chan, &dmaConf, true);             

    // cs irq
    g_bus_rx_gpio[bus->rx_pin_base + bus->rx_buswidth + 1] = bus;
    gpio_set_irq_enabled_with_callback(bus->rx_pin_base + bus->rx_buswidth + 1, GPIO_IRQ_EDGE_FALL, true, &rx_gpio_cs_callback);
    
    // ack output
    gpio_init( bus->rx_ack_pin );
    gpio_set_dir( bus->rx_ack_pin, 1 );
    gpio_put( bus->rx_ack_pin, 0 );
    
    // Enable rx sm
    pio_sm_set_enabled(bus->rx_pio, bus->rx_sm, true);      
}


void bus_rx_set_callback(BusRx_t* bus, BusMsgHandler_t irq_handler, BusMsgHandler_t main_handler)
{
    bus->rx_irq_handler = irq_handler;
    bus->rx_main_handler = main_handler;    
}


void bus_rx_push_defer_cmd(BusRx_t* bus, Cmd_Header_t* cmd)
{
    // Mark as pending  
    bus->rx_pending_buffer = bus->rx_buffer;
    bus->rx_pending_time = time_us_32();
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

void bus_rx_ack_deferred_cmd(BusRx_t* bus, Cmd_Header_t* cmd)
{
    bus->rx_ack_cnt++;
    bus->rx_pending_buffer = 0;
    gpio_put( bus->rx_ack_pin, 1);
    gpio_put( bus->rx_ack_pin, 0);
}


//
// ===================================================
void reset_pico_gpio()
{
    for(int i=0;i<32;i++)
    {
        gpio_deinit(i);        
    }
}


void tx_dma_handler_impl( BusTx_t* bus ) 
{    
    assert(bus);
    if(bus->tx_irq == 0)
        dma_hw->ints0 = 1u << bus->tx_dma_chan;
    else
        dma_hw->ints1 = 1u << bus->tx_dma_chan;
    bus->tx_dma_done = true;
}

void tx_dma_handler_pio0_sm0() 
{
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(0,0) ] );
}
void tx_dma_handler_pio0_sm1() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(0,1) ] );
}
void tx_dma_handler_pio0_sm2() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(0,2) ] );
}
void tx_dma_handler_pio0_sm3() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(0,3) ] );
}

void tx_dma_handler_pio1_sm0() 
{
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(1,0) ] );
}
void tx_dma_handler_pio1_sm1() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(1,1) ] );
}
void tx_dma_handler_pio1_sm2() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(1,2) ] );
}
void tx_dma_handler_pio1_sm3() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(1,3) ] );
}

void tx_dma_handler_pio2_sm0() 
{
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(2,0) ] );
}
void tx_dma_handler_pio2_sm1() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(2,1) ] );
}
void tx_dma_handler_pio2_sm2() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(2,2) ] );
}
void tx_dma_handler_pio2_sm3() 
{    
    tx_dma_handler_impl( g_bus_tx_pio_sm[ PIOSM_ID_TO_INDEX(2,3) ] );
}

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
    uint32_t pio_index = pio_get_index(bus->tx_pio);    

    // init cache
    prog_cache_init(pio_index);

    // init
    for(int i=0;i<bus->tx_buswidth + 2;i++)
    {
        gpio_init(bus->tx_pin_base+i);
        gpio_set_dir(bus->tx_pin_base+i, true);        
    }

    gpio_init( bus->tx_ack_pin );

    queue_init(&bus->tx_responseQueue, sizeof(Cmd_Header_t*), BUS_TX_RESPONSE_MAX_QUEUE);
    queue_init(&bus->tx_requestQueue, sizeof(Cmd_Header_t*), BUS_TX_REQUEST_MAX_QUEUE);

    pio_sm_config c;
    switch (bus->tx_buswidth) {
        case 1:            
            if(g_pio_program_cache[pio_index].bus_tx_1b_offset == -1)
            {
               g_pio_program_cache[pio_index].bus_tx_1b_offset = pio_add_program(bus->tx_pio, &bus_tx_1b_program);
            }
            bus->tx_offset = g_pio_program_cache[pio_index].bus_tx_1b_offset;

            c = bus_tx_1b_program_get_default_config(bus->tx_offset);
            bus->tx_byteloop_offset = bus->tx_offset + bus_tx_1b_offset_byteloop;
            bus->tx_idle_disarmed_offset = bus->tx_offset + bus_tx_1b_offset_byteloop;
            break;
        case 2:            
            if(g_pio_program_cache[pio_index].bus_tx_2b_offset == -1)
            {
               g_pio_program_cache[pio_index].bus_tx_2b_offset = pio_add_program(bus->tx_pio, &bus_tx_2b_program);
            }
            bus->tx_offset = g_pio_program_cache[pio_index].bus_tx_2b_offset;

            c = bus_tx_2b_program_get_default_config(bus->tx_offset);
            bus->tx_byteloop_offset = bus->tx_offset + bus_tx_2b_offset_byteloop;
            bus->tx_idle_disarmed_offset = bus->tx_offset + bus_tx_2b_offset_byteloop;
            break;            
        case 4:
            if(g_pio_program_cache[pio_index].bus_tx_4b_offset == -1)
            {
               g_pio_program_cache[pio_index].bus_tx_4b_offset = pio_add_program(bus->tx_pio, &bus_tx_4b_program);
            }
            bus->tx_offset = g_pio_program_cache[pio_index].bus_tx_4b_offset;

            c = bus_tx_4b_program_get_default_config(bus->tx_offset);
            bus->tx_byteloop_offset = bus->tx_offset + bus_tx_4b_offset_byteloop;
            bus->tx_idle_disarmed_offset = bus->tx_offset + bus_tx_4b_offset_byteloop;
            break;
        case 8:
            if(g_pio_program_cache[pio_index].bus_tx_8b_offset == -1)
            {
               g_pio_program_cache[pio_index].bus_tx_8b_offset = pio_add_program(bus->tx_pio, &bus_tx_8b_program);
            }
            bus->tx_offset = g_pio_program_cache[pio_index].bus_tx_8b_offset;

            c = bus_tx_8b_program_get_default_config(bus->tx_offset);
            bus->tx_byteloop_offset = bus->tx_offset + bus_tx_8b_offset_byteloop;
            bus->tx_idle_disarmed_offset = bus->tx_offset + bus_tx_8b_offset_byteloop;
            break;                                
        default:
            panic("Invalid bus width");
    }
    if(bus->tx_offset < 0)
    {
        panic("failed to add PIO tx program");
    }

    sm_config_set_out_pins(&c, bus->tx_pin_base, bus->tx_buswidth + 2);

    for(int i=0;i<bus->tx_buswidth + 2;i++)
        pio_gpio_init(bus->tx_pio, bus->tx_pin_base+i);

    pio_sm_set_consecutive_pindirs(bus->tx_pio, bus->tx_sm, bus->tx_pin_base, bus->tx_buswidth + 2, true);
    sm_config_set_out_shift(&c, false, false, 8);  // autopush not enabled, 8 bits setting not used     
    sm_config_set_sideset_pins(&c, bus->tx_pin_base+bus->tx_buswidth);

    if(bus->tx_div < 0)
        panic("Invalid bus->tx_div");
    sm_config_set_clkdiv(&c, bus->tx_div);

    pio_sm_init(bus->tx_pio, bus->tx_sm, bus->tx_offset, &c);
    
    int pc,v;
    
    // set byte counter
    int x_count = 8-1; // init
    pio_write_reg(bus->tx_pio, bus->tx_sm, pio_x, x_count );

    v = pio_read_reg(bus->tx_pio, bus->tx_sm, pio_x);
    if( v != x_count)
        panic("Failed to init PIO tx counter");

    pio_sm_set_enabled(bus->tx_pio, bus->tx_sm, true);      


    // TX DMA setup        
    bus->tx_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dmaConfig = dma_channel_get_default_config(bus->tx_dma_chan);        
    channel_config_set_transfer_data_size(&dmaConfig, DMA_SIZE_8);
    channel_config_set_read_increment(&dmaConfig, true);

    volatile void *write_addr = 0;
    int dreq = 0;
    irq_handler_t handler = 0;
    switch(pio_get_index(bus->tx_pio))
    {
        case 0:
            write_addr = &pio0_hw->txf[bus->tx_sm];
            dreq = DREQ_PIO0_TX0 + bus->tx_sm;
            switch (bus->tx_sm) {
                case 0:
                    handler = tx_dma_handler_pio0_sm0;   
                    break;
                case 1:
                    handler = tx_dma_handler_pio0_sm1;   
                    break;
                case 2:
                    handler = tx_dma_handler_pio0_sm2;   
                    break;
                case 3:
                    handler = tx_dma_handler_pio0_sm3;   
                    break;                                                            
            }            
            break;
        case 1:
            write_addr = &pio1_hw->txf[bus->tx_sm];
            dreq = DREQ_PIO1_TX0 + bus->tx_sm;
            switch (bus->tx_sm) {
                case 0:
                    handler = tx_dma_handler_pio1_sm0;   
                    break;
                case 1:
                    handler = tx_dma_handler_pio1_sm1;   
                    break;
                case 2:
                    handler = tx_dma_handler_pio1_sm2;   
                    break;
                case 3:
                    handler = tx_dma_handler_pio1_sm3;   
                    break;                                                            
            }   
            break;
#if PICO_PIO_VERSION > 0            
        case 2:
            write_addr = &pio2_hw->txf[bus->tx_sm];
            dreq = DREQ_PIO2_TX0 + bus->tx_sm;
            switch (bus->tx_sm) {
                case 0:
                    handler = tx_dma_handler_pio2_sm0;   
                    break;
                case 1:
                    handler = tx_dma_handler_pio2_sm1;   
                    break;
                case 2:
                    handler = tx_dma_handler_pio2_sm2;   
                    break;
                case 3:
                    handler = tx_dma_handler_pio2_sm3;   
                    break;                                                            
            }   
            break;                                
#endif            
    }
    channel_config_set_dreq(&dmaConfig, dreq);
    dma_channel_configure(
        bus->tx_dma_chan,
        &dmaConfig,
        write_addr, // Write address (only need to set this once)
        NULL,             // Don't provide a read address yet
        0, // Write the same value many times, then halt and interrupt
        false             // Don't start yet
    );

    if(bus->tx_irq == 0)
        dma_channel_set_irq0_enabled(bus->tx_dma_chan, true);
    else
        dma_channel_set_irq1_enabled(bus->tx_dma_chan, true);

    // Configure the processor to run dma_handler() when DMA IRQ 0 is asserted
    //irq_set_exclusive_handler(DMA_IRQ_0 + bus->tx_irq, handler);
    // NOTE: add ```add_compile_definitions(PICO_MAX_SHARED_IRQ_HANDLERS=8)``` to cmake to allow more
    irq_add_shared_handler(DMA_IRQ_0 + bus->tx_irq, handler, 0);
    irq_set_enabled(DMA_IRQ_0 + bus->tx_irq, true); 
    
    // ack irq
    g_bus_tx_gpio[bus->tx_ack_pin] = bus;
    gpio_set_irq_enabled_with_callback(bus->tx_ack_pin, GPIO_IRQ_EDGE_FALL, true, &rx_gpio_cs_callback);    

    // map bus index
    g_bus_tx_pio_sm[PIOSM_ID_TO_INDEX(pio_index, bus->tx_sm)] = bus;
    g_bus_tx_pio[pio_index] = bus; // claim for tx

    if(!mutex_is_initialized(&g_pio_lock[pio_index]))
    {
        mutex_init(&g_pio_lock[pio_index]);
    }

    bus->tx_dma_done = true;
}


void bus_tx_reset(BusTx_t* bus)
{
    while(!bus->tx_dma_done)
    {
        tight_loop_contents();
        printf("wait\n"); // [optim] bug
    }

    while(queue_get_level(&bus->tx_responseQueue))
    {
        Cmd_Header_t* nextCmd;
        queue_try_remove(&bus->tx_responseQueue, &nextCmd);
    }

    while(queue_get_level(&bus->tx_requestQueue))
    {
        Cmd_Header_t* nextCmd;
        queue_try_remove(&bus->tx_requestQueue, &nextCmd);
    }

    bus->tx_total_tx_bytes = 0;
    bus->tx_total_send_cmd_cnt = 0;
    bus->tx_rpc_id = 0;
    bus->rx_ack_cnt = 0;
    bus->rx_ack_cnt_inc_time = 0;
    bus->rx_pending_ack_cnt = 0;
    bus->rx_pending_ack_inc_time = 0;
    bus->tx_ack_timeout = 0;
    bus->tx_ack_timeout_cnt = 0;
    bus->tx_rpc_timeout_cnt = 0;
    bus->next_ack_handler = 0;
    bus->last_write_buffer = 0;

    // stats
    bus->last_total_tx_bytes = 0;
    bus->last_total_tx_time = 0;
    bus->queue_request_main_overflow = 0;
    bus->queue_response_main_overflow = 0;
}


void bus_tx_set_debugger(BusTx_t* bus)
{
    bus->tx_ack_timeout = 0;
}


static bool bus_tx_is_done(BusTx_t* bus)
{
    // timeout prev tx ack
    if(bus->rx_ack_cnt != bus->rx_pending_ack_cnt)
    {
        if(bus->tx_ack_timeout > 0 && time_us_32() - bus->rx_pending_ack_inc_time > bus->tx_ack_timeout)
        {
            bus->rx_ack_cnt = bus->rx_pending_ack_cnt;
            bus->tx_ack_timeout_cnt++;
        }
    }

    return bus->tx_dma_done 
        && pio_sm_get_pc(bus->tx_pio, bus->tx_sm) >= bus->tx_idle_disarmed_offset 
        && bus->rx_ack_cnt == bus->rx_pending_ack_cnt;
}


void bus_tx_write_async(BusTx_t* bus, uint8_t* buffer, int sz)
{
    //printf("[%s,%d,%d]bus_tx_write_async %P, %d\n", bus->name, time_us_32(), bus->rx_ack_cnt, buffer, sz);

    bus->lastSeqNum++;

    struct Cmd_Header_t* frame = (struct Cmd_Header_t*)buffer;
    frame->seqNum = bus->lastSeqNum;

    if(sz > bus->max_tx_size)
        panic("max packet size");

    // ensure not busy, block until dma completion
    while (!bus_tx_is_done(bus)) 
    {        
        tight_loop_contents();        
        sleep_us(0);
    }

    // Ensure wait was called
    if(bus->rx_pending_ack_cnt != bus->rx_ack_cnt)
        panic("writing to bus with pending ack");

    // next ack
    bus->rx_pending_ack_cnt++;
    bus->rx_pending_ack_inc_time = time_us_32();

    mutex_enter_blocking(&g_pio_lock[pio_get_index(bus->tx_pio)]);

    // restart tx
    pio_sm_set_enabled(bus->tx_pio, bus->tx_sm, false);   
    pio_sm_clear_fifos(bus->tx_pio, bus->tx_sm);
    pio_sm_restart(bus->tx_pio, bus->tx_sm);  

    pio_sm_exec_wait_blocking(bus->tx_pio, bus->tx_sm, pio_encode_jmp(bus->tx_byteloop_offset));

    // set byte counter
    int x_count = sz-1;
    pio_write_reg(bus->tx_pio, bus->tx_sm, pio_x, x_count );

    int v = pio_read_reg(bus->tx_pio, bus->tx_sm, pio_x);
    if( v != x_count)
    {
        // hmm, getting failure yet works 2nd time
        pio_write_reg(bus->tx_pio, bus->tx_sm, pio_x, x_count );
        v = pio_read_reg(bus->tx_pio, bus->tx_sm, pio_x);
        if( v != x_count)
            panic("Failed to init PIO tx counter");
    }

    // reset DMA
    bus->tx_dma_done = false;
    if(bus->tx_irq == 0)
        dma_hw->ints0 = 1u << bus->tx_dma_chan;
    else
        dma_hw->ints1 = 1u << bus->tx_dma_chan; 

    bus->last_write_buffer = buffer;

    // start and pull will success and start transmitting bytes
    dma_channel_transfer_from_buffer_now(bus->tx_dma_chan, (uint8_t*)buffer, sz);       

    // Re-enable PIO
    pio_sm_set_enabled(bus->tx_pio, bus->tx_sm, true);   

    mutex_exit(&g_pio_lock[pio_get_index(bus->tx_pio)]);

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

    //printf("bus_tx_write_cmd_async %d cmd: %d, sz: %d\n", frameOut->cmd, frameOut->sz);

    bus_tx_write_async(bus, (uint8_t*)frameOut, frameOut->sz);
}


void bus_tx_wait(BusTx_t* bus)
{
    while (!bus_tx_is_done(bus)) 
    {        
        tight_loop_contents();        
        sleep_us(0);
    }
}


bool bus_tx_is_busy(BusTx_t* bus)
{
    return !bus_tx_is_done(bus);
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


bool bus_tx_queue_request_from_irq(BusTx_t* bus, Cmd_Header_t* frameOut)
{
    if(frameOut->sz >= bus->max_tx_size)
        panic("max packet size");

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
        panic("max packet size");

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


int bus_tx_update(BusTx_t* bus)
{
     int result = 0;
    Cmd_Header_t* nextCmd;
    while(queue_get_level(&bus->tx_responseQueue) || queue_get_level(&bus->tx_requestQueue))
    {
        // break when busy to keep things async
        if(bus_tx_is_busy(bus))
            break;;
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
            break;;
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

int bus_tx_request_blocking(BusTx_t* bus_tx, BusRx_t* bus_rx, Cmd_Header_t* frameOut, Cmd_Header_t* frameResponse, size_t responseSize, uint32_t timeoutMs)
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

    uint32_t startTime = time_us_32();

    // wait loop
    while(1)
    {    
        bus_tx_update(bus_tx);
        picocom_wdt();

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

        if( time_us_32() - startTime > timeoutUs )
        {
            bus_tx->tx_rpc_timeout_cnt++;
            return SDKErr_Fail; // Timeout
        }
    }

    // cleanup
    return SDKErr_Fail;
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


int bus_txrx_rpc_set_return_main(BusTx_t* bus_tx, BusRx_t* bus_rx, Cmd_Header_t* reqFrameOut, Cmd_Header_t* frameOut)
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


int bus_tx_rpc_set_return_irq(BusTx_t* bus_tx, Cmd_Header_t* reqFrameOut, Cmd_Header_t* frameOut)
{
    // Link required to match request on sender end
    frameOut->cmd = reqFrameOut->cmd;
    frameOut->id = reqFrameOut->id;

    // Queue
    return bus_tx_queue_request_from_irq(bus_tx, frameOut);    
}


void bus_tx_pulse_debug_outputs(BusTx_t* bus)
{
    // init
    for(int i=0;i<bus->tx_buswidth + 2;i++)
    {
        gpio_init(bus->tx_pin_base+i);
        gpio_set_dir(bus->tx_pin_base+i, true);        
    }

    gpio_init( bus->tx_ack_pin );
    
    int isOn = 0;
    while(1)   
    {
        for(int i=0;i<bus->tx_buswidth + 2;i++)
        {            
            gpio_put(bus->tx_pin_base+i, isOn);        
        }

        isOn = !isOn;
        sleep_ms(1000);

        int ack = gpio_get(bus->tx_ack_pin);
        if(ack)
        {
            ack = ack; // debug bp trig
            printf("optimize my ball sack\n");
        }
        printf("ack %d\n", ack);
    }
}


void bus_tx_pulse_debug_input(BusRx_t* bus)
{
    for(int i=0;i<bus->rx_buswidth + 2;i++) // 8 bits + clk,cs    
    {
        gpio_init( bus->rx_pin_base + i );
        gpio_set_dir( bus->rx_pin_base + i, false );
    }
     
    // ack output
    gpio_init( bus->rx_ack_pin );
    gpio_set_dir( bus->rx_ack_pin, 1 );
    gpio_put( bus->rx_ack_pin, 0 );
    
    int isOn = 0;
    while(1)   
    {
        gpio_put(bus->rx_ack_pin, isOn);      
        isOn = !isOn;
        sleep_ms(1000);

        uint32_t bits = 0;
        for(int i=0;i<bus->rx_buswidth + 2;i++) // 8 bits + clk,cs    
        {
            int bit = bus->rx_pin_base + i;
            int isBitSet = gpio_get(bit);
            if(isBitSet)
            {
                isBitSet = isBitSet; // bp trig
                printf("optimize my ball sack\n");
            }
            
            bits |= (isBitSet << i);
        }

        if(bits)
        {
            bits = bits; // bp trig            
        }

        printf("bits: 0x%x\n", bits);
    }
}


void bus_tx_set_ack_callback(BusTx_t* bus, BusMsgAckHandler_t ack_handler)
{
    bus->ack_handler = ack_handler;
}


void bus_rx_update_stats(BusRx_t* bus_rx, struct Res_Bus_Diag_Stats* stats)
{
    stats->totalBytesRx = bus_rx->rx_total_rx_bytes;
    stats->busErrorsRx = bus_rx->rx_invalidHeaderCnt;
    stats->busErrorsRx += bus_rx->rx_pendingCmdNotProcessedErrCnt;      

    uint32_t t1 = time_us_32(); 
    double deltaS = (t1-bus_rx->last_total_rx_time) / 1000000.0f;
    int diff = bus_rx->rx_total_rx_bytes - bus_rx->last_total_rx_bytes;
    double byteSec = diff / deltaS;

    if(diff > 1.0)
    {
        bus_rx->last_total_rx_bytes = bus_rx->rx_total_rx_bytes;            
        bus_rx->last_total_rx_time = time_us_32(); 
    }

    stats->busRateRx = byteSec;      
}


void bus_rx_update(struct BusRx_t* busRx)
{
    Cmd_Header_t* frame = bus_rx_get_next_deferred_cmd( busRx );
    if( frame ) 
    {
        bus_rx_dispatch_main_cmd(busRx, frame);
    }
}


void bus_tx_update_stats(BusTx_t* bus_tx, struct Res_Bus_Diag_Stats* stats)
{
    stats->totalBytesTx = bus_tx->tx_total_tx_bytes;

    stats->busErrorsTx = bus_tx->tx_ack_timeout_cnt;
    stats->busErrorsTx += bus_tx->tx_rpc_timeout_cnt;
    stats->busErrorsTx += bus_tx->queue_request_main_overflow;
    stats->busErrorsTx += bus_tx->queue_response_main_overflow;

    uint32_t t1 = time_us_32(); 
    double deltaS = (t1-bus_tx->last_total_tx_time) / 1000000.0f;
    int diff = bus_tx->tx_total_tx_bytes - bus_tx->last_total_tx_bytes;
    double byteSec = diff / deltaS;

    // sample every 1s
    if(diff > 1.0)
    {
        bus_tx->last_total_tx_bytes = bus_tx->tx_total_tx_bytes;            
        bus_tx->last_total_tx_time = time_us_32(); 
    }

    stats->busRateTx = byteSec; 
}

void bus_debug_print_frame(Cmd_Header_t* frame)
{
    if(!frame)
    {
        printf("null\n");
        return;
    }

    uint8_t* ptr = (uint8_t*)frame;
    printf("Frame(0x%x,%d,sz:%d,cmd:%d) ", frame->magic, frame->id, frame->sz, frame->cmd);
    for(int i=0;i<frame->sz;i++)
    {
        printf("%02X ", ptr[i]);
    }

    printf("\n");
}
