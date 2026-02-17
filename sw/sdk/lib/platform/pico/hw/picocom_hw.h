#pragma once

#include "stdint.h"

#ifdef PICOCOM_SDL 
#include "lib/components/mock_hardware/pio.h"
#else
#include "hardware/pio.h"
#endif

//#define PICOHW_SLOW_AND_SAFE          // Enable to select slowest clock speed ( when having signalling issues on breadboards )

#ifdef __cplusplus
extern "C" {
#endif

//
// Devices ===================================

/** Unique chip/device ids */
enum EPicocomDeviceIds
{
    EPicocomDeviceIds_APP,
    EPicocomDeviceIds_VDP1, 
    EPicocomDeviceIds_VDP2,    
    EPicocomDeviceIds_APU,
    EPicocomDeviceIds_MaxDevice
};

//
// APU Pins ===================================


// ALNK_RX 1bit bus to APP receive bus, reads APU commands from main app cpu
#define APU_ALNK_RX_PIO        pio1    // [rx] XLNK pio device
#define APU_ALNK_RX_SM         1       // [rx] XLNK pio sm device
#define APU_ALNK_RX_D0_PIN     2       // [rx][in] XLNK input base pin (0-1)
#define APU_ALNK_RX_DATA_CNT   1       // [rx] XLNK data width
#define APU_ALNK_RX_ACK_PIN    12      // [rx][out[ XLNK act output

// ALNK TX 1bit bus to APP send bus, APU status commands and readback for app apu
#define APU_ALNK_TX_PIO        pio0    // [tx] VDBUS pio device
#define APU_ALNK_TX_SM         1       // [tx] VDBUS pio sm device
#define APU_ALNK_TX_IRQ        0       // [tx] VDBUS irq index
#define APU_ALNK_TX_D0_PIN     5       // [tx][out] VDBUS base pin (0-8)
#define APU_ALNK_TX_DATA_CNT   1       // [tx] VDBUS data width
#define APU_ALNK_TX_ACK_PIN    13      // [tx][in] VDBUS ack input pin

// From RGB neopixel LED ring
#define APU_WS2812_PIO          pio2
#define APU_WS2812_PIN          15
#define APU_WS2812_PIXELS       16

// Audio DAC I2S
#define APU_I2S_PIO             pio2
#define APU_I2S_SM              1
#define APU_I2S_XMT             15      // XMT pin (enable output)


// SD card 
#define APU_SDCARD_SPI          spi0

// CPU Config
#define APU_CLOCK_KHZ          0  // cpu clock khz (default clock)

//
// VPD1 Pins ===================================

// VDBUS 8bit wide gfx data write bus, used to write GPU commands to VDP2 from VDP1
#define VDP1_VDBUS_PIO          pio2    // [tx] VDBUS pio device
#define VDP1_VDBUS_SM           0       // [tx] VDBUS pio sm device
#define VDP1_VDBUS_IRQ          0       // [tx] VDBUS irq index
#define VDP1_VDBUS_D0_PIN       2       // [tx][out] VDBUS base pin (0-8)
#define VDP1_VDBUS_DATA_CNT     8       // [tx] VDBUS data width
#define VDP1_VDBUS_ACK_PIN      22      // [tx][in] VDBUS ack input pin
#define VDP1_VDBUS_DIV          2.0     // Bus speed div

// XLINK 1bit VDP2 to VDP1 receive bus, reads commands from VDP2
#define VDP1_XLNK_PIO           pio1    // [rx] XLNK pio device
#define VDP1_XLNK_SM            0       // [rx] XLNK pio sm device
#define VDP1_XLNK_D0_PIN        18      // [rx][in] XLNK input base pin (0-1)
#define VDP1_XLNK_DATA_CNT      1       // [rx] XLNK data width
#define VDP1_XLNK_ACK_PIN       21      // [rx][out[ XLNK act output
#define XLNK_DIV                32.0    // Bus speed div

// VLNK_RX 1bit bus to APP receive bus, reads GPU commands from main app cpu
#define VDP1_VLNK_RX_PIO        pio1    // [rx] XLNK pio device
#define VDP1_VLNK_RX_SM         1       // [rx] XLNK pio sm device
#define VDP1_VLNK_RX_D0_PIN     12      // [rx][in] XLNK input base pin (0-1)
#define VDP1_VLNK_RX_DATA_CNT   1       // [rx] XLNK data width
#define VDP1_VLNK_RX_ACK_PIN    26      // [rx][out[ XLNK act output
#ifdef PICOHW_SLOW_AND_SAFE
    #define VLNK_DIV                32.0     // Bus speed div
#else
    #define VLNK_DIV                3.0     // Bus speed div
#endif

// VLNK TX 1bit bus to APP send bus, gpu status commands and readback for app apu
#define VDP1_VLNK_TX_PIO        pio0    // [tx] VDBUS pio device
#define VDP1_VLNK_TX_SM         1       // [tx] VDBUS pio sm device
#define VDP1_VLNK_TX_IRQ        0       // [tx] VDBUS irq index
#define VDP1_VLNK_TX_D0_PIN     15      // [tx][out] VDBUS base pin (0-8)
#define VDP1_VLNK_TX_DATA_CNT   1       // [tx] VDBUS data width
#define VDP1_VLNK_TX_ACK_PIN    27      // [tx][in] VDBUS ack input pin

// CPU Config
#define VDP1_CLOCK_KHZ          252000  // cpu clock khz

//
// VPD2 Pins ===================================

//
// VDBUS 8bit wide gfx data write bus, used to write GPU commands to VDP2 from VDP1
#define VDP2_VDBUS_PIO          pio0    // [rx] VDBUS pio device
#define VDP2_VDBUS_SM           0       // [rx] VDBUS pio sm device
#define VDP2_VDBUS_D0_PIN       2       // [rx][in] VDBUS base pin (0-8)
#define VDP2_VDBUS_DATA_CNT     8       // [rx] VDBUS data width
#define VDP2_VDBUS_ACK_PIN      27      // [rx][out] VDBUS ack input pin

// XLINK 1bit VDP2 to VDP1 recieve bus, reads commands from VDP2
#define VDP2_XLNK_PIO           pio1    // [tx] XLNK pio device
#define VDP2_XLNK_SM            0       // [tx] XLNK pio sm device
#define VDP2_XLNK_IRQ           1       // [tx] XLNK irq index
#define VDP2_XLNK_D0_PIN        20      // [rx][in] XLNK input base pin (0-1)
#define VDP2_XLNK_DATA_CNT      1       // [rx] XLNK data width
#define VDP2_XLNK_ACK_PIN       26      // [rx][out[ XLNK act output

// CPU Config
#define VDP2_CLOCK_KHZ          252000  // cpu clock khz


//
// APP Pins ======================================

// APP VDBUS_TX 1bit bus to VDP, sends GPU commands to VDP1
#define APP_VLNK_TX_PIO        pio0    // [tx] VDBUS pio device
#define APP_VLNK_TX_SM         0       // [tx] VDBUS pio sm device
#define APP_VLNK_TX_IRQ        0       // [tx] VDBUS irq index
#define APP_VLNK_TX_D0_PIN     2       // [tx][out] VDBUS base pin (0-1)
#define APP_VLNK_TX_DATA_CNT   1       // [tx] VDBUS data width
#define APP_VLNK_TX_ACK_PIN    9       // [tx][in] VDBUS ack input pin
#ifdef PICOHW_SLOW_AND_SAFE
    #define APP_VLNK_TX_DIV        32.0     // [SLOW] Bus speed div 
#else
    #define APP_VLNK_TX_DIV        3.0     // Bus speed div 
#endif

// APP VDBUS_RX 1bit bus to APP receive bus, read gpu status from VDP1 and VDP2
#define APP_VLNK_RX_PIO        pio1    // [rx] VDBUS pio device
#define APP_VLNK_RX_SM         0       // [rx] VDBUS pio sm device
#define APP_VLNK_RX_D0_PIN     5       // [rx][in] VDBUS input base pin (0-1)
#define APP_VLNK_RX_DATA_CNT   1       // [rx] VDBUS data width
#define APP_VLNK_RX_ACK_PIN    8       // [rx][out[ VDBUS act output
#define APP_VLNK_RX_BUFFER_SZ  1024*8    // Max RX buffer size app can recieve (vdp1->app max packet size)

// APP ALNK_TX 1bit bus to APU, sends Audio commands to APU
#define APP_ALNK_TX_PIO        pio0    // [tx] VDBUS pio device
#define APP_ALNK_TX_SM         1       // [tx] VDBUS pio sm device
#define APP_ALNK_TX_IRQ        0       // [tx] VDBUS irq index
#define APP_ALNK_TX_D0_PIN     19      // [tx][out] VDBUS base pin (0-1)
#define APP_ALNK_TX_DATA_CNT   1       // [tx] VDBUS data width
#define APP_ALNK_TX_ACK_PIN    10       // [tx][in] VDBUS ack input pin

// APP ALNK_RX 1bit bus to APP receive bus, read Audio status from APU
#define APP_ALNK_RX_PIO        pio1    // [rx] VDBUS pio device
#define APP_ALNK_RX_SM         1       // [rx] VDBUS pio sm device
#define APP_ALNK_RX_D0_PIN     16      // [rx][in] VDBUS input base pin (0-1)
#define APP_ALNK_RX_DATA_CNT   1       // [rx] VDBUS data width
#define APP_ALNK_RX_ACK_PIN    11      // [rx][out[ VDBUS act output
//#define ALNK_DIV               6.0     // Bus tx speed div to APU
#define ALNK_DIV               32.0     // Bus tx speed div to APU

// APP Gpio
#define APP_DEVICE_RESET_PIN   28      // [out] Reset lin to VDP1, VDP2 & APU
#define APP_DEVICE_BOOT_TIME   200      // Reset wait boot time

// CPU Config
#define APP_CLOCK_KHZ          252000  // cpu clock khz

// Simulator overrides
#ifdef PICOCOM_SDL 

    // app pio redirector for sim
    extern PIO test_bus_app__vdp1Link_tx;
    extern PIO test_bus_app__vdp1Link_rx;
    extern PIO test_bus_app__apuLink_tx;
    extern PIO test_bus_app__apuLink_rx;

    #undef APP_VLNK_TX_PIO
    #undef APP_VLNK_RX_PIO
    #undef APP_ALNK_TX_PIO
    #undef APP_ALNK_RX_PIO        
    #define APP_VLNK_TX_PIO test_bus_app__vdp1Link_tx
    #define APP_VLNK_RX_PIO test_bus_app__vdp1Link_rx
    #define APP_ALNK_TX_PIO test_bus_app__apuLink_tx
    #define APP_ALNK_RX_PIO test_bus_app__apuLink_rx


    // vdp1 pio redirector for sim
    extern PIO test_bus_vdp1__vdp2_vdbus_tx;
    extern PIO test_bus_vdp1__vdp2_xlnk_rx;
    extern PIO test_bus_vdp1__app_vlnk_rx;
    extern PIO test_bus_vdp1__app_vlnk_tx;

    #undef VDP1_VDBUS_PIO
    #undef VDP1_XLNK_PIO
    #undef VDP1_VLNK_RX_PIO
    #undef VDP1_VLNK_TX_PIO
    #define VDP1_VDBUS_PIO test_bus_vdp1__vdp2_vdbus_tx
    #define VDP1_XLNK_PIO test_bus_vdp1__vdp2_xlnk_rx
    #define VDP1_VLNK_RX_PIO test_bus_vdp1__app_vlnk_rx
    #define VDP1_VLNK_TX_PIO test_bus_vdp1__app_vlnk_tx


    // vdp2 pio redirector for sim
    extern PIO test_bus_vdp2__vdp1_vdbus_rx;
    extern PIO test_bus_vdp2__vdp1_xlnk_tx;

    #undef VDP2_VDBUS_PIO
    #undef VDP2_XLNK_PIO
    #define VDP2_VDBUS_PIO test_bus_vdp2__vdp1_vdbus_rx
    #define VDP2_XLNK_PIO test_bus_vdp2__vdp1_xlnk_tx


    // apu pio redirector for sim
    extern PIO test_bus_apu__app_alnk_tx;
    extern PIO test_bus_apu__app_alnk_rx;
    
    #undef APU_ALNK_RX_PIO
    #undef APU_ALNK_TX_PIO
    #define APU_ALNK_RX_PIO test_bus_apu__app_alnk_rx
    #define APU_ALNK_TX_PIO test_bus_apu__app_alnk_tx
#endif

// api
void picocom_hw_init_led_only(bool initBlink);    // init leds and do init blink seq ( blocks )
void picocom_hw_reset();    // Reset hw


#ifdef __cplusplus
}
#endif
