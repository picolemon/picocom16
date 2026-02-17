/** Simulated vdp2 core
 * - runs vdp2_main same as pico, inject hardware abstractions
 * - run vdp2 core with bus & display config
 * - display frame buffer abstracted in display_driver_* lib
 * Note:
 *  - hw main will just pass pio and a similar config
 */
#include "lib/components/vdp2_core/vdp2_core.h"
#include "picocom/display/display.h"
#include "platform/pico/hw/picocom_hw.h"
#include "platform/pico/vdp2/hw_vdp2_types.h"
#ifdef PICOCOM_SDL
#include "lib/platform/sdl2/display/sdl_display_driver.h"
#endif

#include <string.h>
#include <stdio.h>

struct vdp2_t* g_vdp2 = 0;

void test_core_vdp2()
{
    // sim setup
#ifdef PICOCOM_SDL    
    sdl_display_set_window_scale(4);
    sdl_display_set_window_title("picocom16 simulator");    
#endif
    
    struct vdp2MainLoopOptions_t mainOptions;
    struct vdp2InitOptions_t vdpOptions;
    g_vdp2 = (struct vdp2_t*)picocom_malloc(sizeof(struct vdp2_t));
    memset(g_vdp2, 0, sizeof(struct vdp2_t));
    memset(&mainOptions, 0, sizeof(mainOptions));
    memset(&vdpOptions, 0, sizeof(vdpOptions));

    // Create bus links
    static BusTx_t vdp1_xlnk_tx;
    static BusRx_t vdp1_vdbus_rx;    
    
    bus_rx_configure(&vdp1_vdbus_rx,
            VDP2_VDBUS_PIO,
            VDP2_VDBUS_SM,       
            VDP2_VDBUS_DATA_CNT,
            VDP2_VDBUS_D0_PIN,
            VDP2_VDBUS_ACK_PIN
        );
    vdp1_vdbus_rx.name = "(vdp2)vdp1_vdbus_rx";    
    vdp1_vdbus_rx.rx_buffer_size = MAX(sizeof(struct VDP2CMD_TileFrameBuffer16bpp), 1024); // Alloc room for tile buffer
    bus_rx_init(&vdp1_vdbus_rx);
    
    bus_tx_configure(
        &vdp1_xlnk_tx,
        VDP2_XLNK_PIO,
        VDP2_XLNK_SM,
        VDP2_XLNK_DATA_CNT,
        VDP2_XLNK_D0_PIN,
        VDP2_XLNK_ACK_PIN,
        VDP2_XLNK_IRQ,
        XLNK_DIV
    );
    vdp1_xlnk_tx.name = "(vdp2)vdp1_xlnk_tx";
    bus_tx_init( &vdp1_xlnk_tx );

    vdpOptions.vdp1_xlnk_tx = &vdp1_xlnk_tx;
    vdpOptions.vdp1_vdbus_rx = &vdp1_vdbus_rx;
    vdpOptions.gpuOptions.bufferRamSz = VDP2_GPU_RAM_SZ;
    vdpOptions.initDisplayDriver = 1;
    vdpOptions.gpuOptions.enableFlash = true;

    // init vdp core
    int res;
    res = vdp2_init(g_vdp2, &vdpOptions);
    if(res != SDKErr_OK)
    {
        printf("vdp2_init failed %d\n", res);
        return;
    }

#ifdef PICOCOM_NATIVE_SIM
    // see test_core_vdp2_update
#else
    // run mainloop
    vdp2_main(g_vdp2, &mainOptions);
#endif    
}

void test_core_vdp2_update()
{
    vdp2_update(g_vdp2, 0); 
}
