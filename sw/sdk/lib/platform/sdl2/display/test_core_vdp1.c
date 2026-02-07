/** Simulated vdp2 core
 * - runs vdp1_main same as pico, inject hardware abstractions
 * - run vdp2 core with bus & display config
 * - display frame buffer abstracted in display_driver_* lib
 */
#include "lib/components/vdp1_core/vdp1_core.h"
#include "platform/pico/hw/picocom_hw.h"
#include "platform/pico/vdp1/hw_vdp1_types.h"
#include <string.h>
#include <stdio.h>

struct vdp1_t* g_vdp1 = 0;

void test_core_vdp1()
{    
    struct vdp1MainLoopOptions_t mainOptions;
    struct vdp1InitOptions_t vdpOptions;
    g_vdp1 = (struct vdp1_t*)picocom_malloc(sizeof(struct vdp1_t));
    memset(g_vdp1, 0, sizeof(struct vdp1_t));
    memset(&mainOptions, 0, sizeof(mainOptions));
    memset(&vdpOptions, 0, sizeof(vdpOptions));

    // Create bus links
    static BusTx_t vdp2_vdbus_tx;
    static BusRx_t vdp2_xlnk_rx;    
    static BusTx_t app_vlnk_tx;
    static BusRx_t app_vlnk_rx;    
    
    // Init bus
    bus_tx_configure(
         &vdp2_vdbus_tx,
        VDP1_VDBUS_PIO,
        VDP1_VDBUS_SM,
        VDP1_VDBUS_DATA_CNT,
        VDP1_VDBUS_D0_PIN,
        VDP1_VDBUS_ACK_PIN,
        VDP1_VDBUS_IRQ,
        VDP1_VDBUS_DIV
    );
    vdp2_vdbus_tx.name = "(vdp1)vdp2_vdbus_tx";
    vdp2_vdbus_tx.max_tx_size = 32768*2; //32k max buffer TODO: header const
    bus_tx_init( &vdp2_vdbus_tx );

    bus_rx_configure(&vdp2_xlnk_rx,
            VDP1_XLNK_PIO,
            VDP1_XLNK_SM,       
            VDP1_XLNK_DATA_CNT,
            VDP1_XLNK_D0_PIN,
            VDP1_XLNK_ACK_PIN
        );    
    vdp2_xlnk_rx.name = "(vdp1)vdp2_xlnk_rx";    
    bus_rx_init(&vdp2_xlnk_rx);    

    bus_rx_configure(&app_vlnk_rx,
            VDP1_VLNK_RX_PIO,
            VDP1_VLNK_RX_SM,       
            VDP1_VLNK_RX_DATA_CNT,
            VDP1_VLNK_RX_D0_PIN,
            VDP1_VLNK_RX_ACK_PIN
        );
    app_vlnk_rx.name = "(vdp1)app_vlnk_rx";
    bus_rx_init(&app_vlnk_rx);
    
    bus_tx_configure(
        &app_vlnk_tx,
        VDP1_VLNK_TX_PIO,
        VDP1_VLNK_TX_SM,
        VDP1_VLNK_TX_DATA_CNT,
        VDP1_VLNK_TX_D0_PIN,
        VDP1_VLNK_TX_ACK_PIN,
        VDP1_VLNK_TX_IRQ,
        VLNK_DIV
    );
    app_vlnk_tx.name = "(vdp1)app_vlnk_tx";    
    app_vlnk_tx.max_tx_size = APP_VLNK_RX_BUFFER_SZ; // Max app rx size ( limited by rx buffer )
    bus_tx_init( &app_vlnk_tx );

    vdpOptions.vdp2_vdbus_tx = &vdp2_vdbus_tx;
    vdpOptions.vdp2_xlnk_rx = &vdp2_xlnk_rx;
    vdpOptions.app_vlnk_tx = &app_vlnk_tx;
    vdpOptions.app_vlnk_rx = &app_vlnk_rx;
    vdpOptions.vdp2CmdFwdWriteBufferSz = 0;
    vdpOptions.gpuOptions.bufferRamSz = VDP1_GPU_RAM_SZ;
    vdpOptions.gpuOptions.enableFlash = true;

    // init vdp core
    int res;
    res = vdp1_init(g_vdp1, &vdpOptions);
    if(res != SDKErr_OK)
    {
        printf("vdp1_init failed %d\n", res);
        return;
    }

#ifdef PICOCOM_NATIVE_SIM
    // see test_core_vdp1_update
#else
    // run main loop
    vdp1_main(g_vdp1, &mainOptions);
#endif    
}


void test_core_vdp1_update(void* userData)
{
    vdp1_update(g_vdp1, 0);
}


void test_service_vdp1_main(void* userData)
{
    vdp1_update(g_vdp1, 0);
}