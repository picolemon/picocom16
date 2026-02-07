/** APU
  - i2s audio driver
  - sdcard driver
  - HID driver for mouse, keyboard and some gamepads
*/
#include "lib/components/apu_core/apu_core.h"
#include "platform/pico/hw/picocom_hw.h"
#include "platform/pico/apu/hw_apu_types.h"
#include "platform/pico/hw/picocom_hw.h"
#include "hardware/clocks.h"
#include <string.h>
#include <stdio.h>


struct apu_t* g_apu = 0;


void main()
{
    if(APU_CLOCK_KHZ)
      set_sys_clock_khz(APU_CLOCK_KHZ, true);
    sleep_ms(10);

    // Show active
    reset_pico_gpio();
    picocom_hw_init_led_only(true);
        
    stdio_init_all();
    printf("apu init\n");

    // Create bus links
    BusTx_t app_alnk_tx;
    BusRx_t app_alnk_rx;    

    // Init apu -> app link  
    bus_rx_configure(&app_alnk_rx,
            APU_ALNK_RX_PIO,
            APU_ALNK_RX_SM,       
            APU_ALNK_RX_DATA_CNT,
            APU_ALNK_RX_D0_PIN,
            APU_ALNK_RX_ACK_PIN
        );
    //bus_tx_pulse_debug_input(&app_alnk_rx);
    bus_rx_init(&app_alnk_rx);

    bus_tx_configure(
        &app_alnk_tx,
        APU_ALNK_TX_PIO,
        APU_ALNK_TX_SM,
        APU_ALNK_TX_DATA_CNT,
        APU_ALNK_TX_D0_PIN,
        APU_ALNK_TX_ACK_PIN,
        APU_ALNK_TX_IRQ,
        ALNK_DIV
    );
    app_alnk_tx.name = "app_alnk_tx";
    bus_tx_init( &app_alnk_tx );
    
    struct apuMainLoopOptions_t mainOptions;
    struct apuInitOptions_t vdpOptions;
    g_apu = (struct apu_t*)picocom_malloc(sizeof(struct apu_t));
    memset(g_apu, 0, sizeof(struct apu_t));
    memset(&mainOptions, 0, sizeof(mainOptions));
    memset(&vdpOptions, 0, sizeof(vdpOptions));

    vdpOptions.app_alnk_tx = &app_alnk_tx;
    vdpOptions.app_alnk_rx = &app_alnk_rx;
    
    // set xmt pin
    gpio_init(APU_I2S_XMT);
    gpio_pull_up(APU_I2S_XMT);

    // init vdp core
    int res;
    res = apu_init(g_apu, &vdpOptions);
    if(res != SDKErr_OK)
    {
        printf("apu_init failed %d\n", res);
        return;
    }
    
    // run main loop
    apu_main(g_apu, &mainOptions);
}