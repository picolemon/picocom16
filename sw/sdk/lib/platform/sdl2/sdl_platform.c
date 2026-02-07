#include "stdio.h"
#include "picocom/devkit.h"
#include "lib/platform/pico/bus/bus.h"
#include "lib/platform/pico/hw/picocom_hw.h"
#include "lib/components/mock_hardware/pio.h"
#include "lib/components/mock_hardware/mock_bus.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// vdp1 state
PIO test_bus_vdp1__vdp2_vdbus_tx;
PIO test_bus_vdp1__vdp2_xlnk_rx;
PIO test_bus_vdp1__app_vlnk_rx;
PIO test_bus_vdp1__app_vlnk_tx;

void test_core_vdp1();

// vdp2 state
PIO test_bus_vdp2__vdp1_vdbus_rx;
PIO test_bus_vdp2__vdp1_xlnk_tx;

void test_core_vdp2();

// apu state

PIO test_bus_apu__app_alnk_tx;
PIO test_bus_apu__app_alnk_rx;

void test_core_apu();

// app state
PIO test_bus_app__vdp1Link_tx;
PIO test_bus_app__vdp1Link_rx;
PIO test_bus_app__apuLink_tx;
PIO test_bus_app__apuLink_rx;

void app_main();

int main()
{    
    struct coreManager_t mgr;
    struct busMockRouter_t router;

    bus_mock_router_create(&router, 1);

    // tx -> rx 
    // link pios, emulate physical wiring (ideally auto map this using enums)
    //      VDP1_VDBUS_PIO          pio2    // [tx] VDBUS pio device    ->  VDP2_VDBUS_PIO          pio0    // [rx] VDBUS pio device
    //      VDP1_XLNK_PIO           pio1    // [rx] XLNK pio device     ->  VDP2_XLNK_PIO           pio1    // [tx] XLNK pio device
    bus_mock_link_pio(&router, &test_bus_vdp1__vdp2_vdbus_tx, &test_bus_vdp2__vdp1_vdbus_rx );
    bus_mock_link_pio(&router, &test_bus_vdp2__vdp1_xlnk_tx, &test_bus_vdp1__vdp2_xlnk_rx );
    test_bus_vdp1__vdp2_vdbus_tx->name = "test_bus_vdp1__vdp2_vdbus_tx->test_bus_vdp2__vdp1_vdbus_rx";
    test_bus_vdp2__vdp1_xlnk_tx->name = "test_bus_vdp2__vdp1_xlnk_tx->test_bus_vdp1__vdp2_xlnk_rx";

    //      APP_VLNK_TX_PIO        pio0    // [tx] VDBUS pio device     ->  VDP1_VLNK_RX_PIO        pio1    // [rx] XLNK pio device
    //      APP_VLNK_RX_PIO        pio1    // [rx] VDBUS pio device     ->  VDP1_VLNK_TX_PIO        pio0    // [tx] VDBUS pio device
    bus_mock_link_pio(&router, &test_bus_app__vdp1Link_tx, &test_bus_vdp1__app_vlnk_rx );
    bus_mock_link_pio(&router, &test_bus_vdp1__app_vlnk_tx, &test_bus_app__vdp1Link_rx );
    test_bus_app__vdp1Link_tx->name = "test_bus_app__vdp1Link_tx->test_bus_vdp1__app_vlnk_rx";
    test_bus_vdp1__app_vlnk_tx->name = "test_bus_vdp1__app_vlnk_tx->test_bus_app__vdp1Link_rx";


    //      APP_ALNK_TX_PIO     ->      APU_ALNK_RX_PIO
    //      APU_ALNK_TX_PIO     ->      APP_ALNK_RX_PIO
    bus_mock_link_pio(&router, &APP_ALNK_TX_PIO, &APU_ALNK_RX_PIO );
    bus_mock_link_pio(&router, &APU_ALNK_TX_PIO, &APP_ALNK_RX_PIO );
    test_bus_apu__app_alnk_tx->name = "APP_ALNK_TX_PIO->APU_ALNK_RX_PIO";
    test_bus_app__apuLink_rx->name = "APU_ALNK_TX_PIO->APP_ALNK_RX_PIO";

    core_manager_create(&mgr); 
    core_manager_launch(&mgr, test_core_vdp1); 
    core_manager_launch(&mgr, test_core_vdp2); 
    core_manager_launch(&mgr, test_core_apu); 
    core_manager_launch(&mgr, app_main);
    
    core_manager_join(&mgr);
    
    return 0;
}


void tight_loop_contents()
{
   // picocom_sleep_us(1);
}


void picocom_hw_init_led_only(bool a)
{

}
