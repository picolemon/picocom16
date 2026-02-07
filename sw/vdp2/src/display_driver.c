//#pragma GCC optimize ("O0")
#include "display_driver.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/systick.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "common_dvi_pin_configs.h"
#include "dvi.h"
#include "dvi_serialiser.h"
#include "tmds_encode.h"
#include "platform/pico/bus/bus.h"
#include "platform/pico/bus/bus_testing.h"
#include "platform/pico/hw/picocom_hw.h"

// 320x240 stable config
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

struct dvi_inst dvi0;
volatile int driver_frame_ctr = 0;
volatile int driver_row_ctr = 0;
queue_t buffer0_queue; // hdmi -> in
queue_t buffer1_queue;  // hdmi release
uint16_t buffer0_data[320*240];
uint16_t buffer1_data[320*240];
uint16_t* buffer0 =  (uint16_t*)buffer0_data; // core0 init
uint16_t* buffer1 =  (uint16_t*)buffer1_data; // core1 init
uint16_t* main_buffer = (uint16_t*)buffer0_data;
bool is_display_inited = false;


int count = 0;
//
//
void core1_main()
{    
#ifndef DISPLAY_NULL        
	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    // wait init frame
    uint16_t* next_buffer = 0;
    queue_remove_blocking_u32(&buffer0_queue, &next_buffer);

    struct dvi_inst *inst = &dvi0;
    dvi_register_irqs_this_core(inst, DMA_IRQ_0);
	dvi_start(inst);

    uint16_t* buffer = (uint16_t*)buffer1;    
	while (true) {
        
        // check buffer swap sync        
        if(!next_buffer) 
        {
	        queue_try_remove (&buffer0_queue, &next_buffer);            
        }
        if(next_buffer)
        {
            // try push current buffer to main ( can be null )
            if( queue_try_add  (&buffer1_queue, &buffer) )
            {
                buffer = next_buffer; // use next, buffer gone to main
                next_buffer = 0; // clear pending
            }
        }
        
        if(!buffer)
            continue;

		for (uint y = 0; y < FRAME_HEIGHT; ++y) {			
			const uint16_t *scanline = &(buffer)[y * FRAME_WIDTH];
			uint32_t *tmdsbuf;
			queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
			
            uint pixwidth = inst->timing->h_active_pixels;
            uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
            tmds_encode_data_channel_16bpp((const uint32_t*)scanline, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_16BPP_BLUE_MSB,  DVI_16BPP_BLUE_LSB );
            tmds_encode_data_channel_16bpp((const uint32_t*)scanline, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
            tmds_encode_data_channel_16bpp((const uint32_t*)scanline, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_16BPP_RED_MSB,   DVI_16BPP_RED_LSB  );            
			queue_add_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);            
            driver_row_ctr = y;
		}

        driver_frame_ctr++;
	}   
#endif
}

//
//
void display_driver_setup_clocks()
{
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
}


//display_driver_init
//
bool display_driver_init()
{
    if(is_display_inited)
        return false;

#ifndef DISPLAY_NULL        
	display_driver_setup_clocks();

    queue_init_with_spinlock(&buffer0_queue,   sizeof(void*),  8, next_striped_spin_lock_num());
    queue_init_with_spinlock(&buffer1_queue,   sizeof(void*),  8, next_striped_spin_lock_num());

	multicore_launch_core1(core1_main);

    // init fill
    for(size_t i=0;i<320*240;i++)
    {
        buffer1[i] = 0b1111100000011111;        
        buffer0[i] =   0b0000000000011111;
    }
#endif    
    is_display_inited = true;
    return true;
}

uint16_t* get_display_buffer()
{
    return main_buffer;
}

uint16_t* flip_display_blocking()
{
    if(!is_display_inited)
        return main_buffer;    
#ifndef DISPLAY_NULL        
    queue_add_blocking_u32(&buffer0_queue, &main_buffer);
    queue_remove_blocking_u32(&buffer1_queue, &main_buffer);    
#endif
    return main_buffer;
}


void display_buffer_copy_front()
{
    if(!is_display_inited)
        return;
#ifndef DISPLAY_NULL    
    uint16_t* src = get_display_buffer() == buffer0 ? buffer0 : buffer1;   // current front
    uint16_t* dst = get_display_buffer() != buffer0 ? buffer0 : buffer1;   // current back
    memcpy(dst, src, sizeof(buffer0_data));
#endif    
}


bool display_driver_get_init()
{
    return is_display_inited;
}