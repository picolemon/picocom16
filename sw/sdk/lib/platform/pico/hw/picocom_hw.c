#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "picocom_hw.h"
#include "picocom/devkit.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"


//
//
void picocom_hw_setup_clocks(PicocomInitOptions_t* options)
{
    if(options->clockSpeedKhz)
        set_sys_clock_khz(options->clockSpeedKhz, true);

    // print init msg as soon as booted for debug
    stdio_init_all();
    printf("%s init\n", options->name);
}

void picocom_hw_init_led_only(bool initBlink)
{
    // led
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);    

    if(initBlink)
    {
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
    }
}


void picocom_hw_init(PicocomInitOptions_t* options)
{
    // led
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);    

    // reset pins
    gpio_init(APP_DEVICE_RESET_PIN);
    gpio_put(APP_DEVICE_RESET_PIN, 1);
    gpio_set_dir(APP_DEVICE_RESET_PIN, GPIO_OUT);    
}


void picocom_hw_reset()
{
    gpio_init(APP_DEVICE_RESET_PIN);
    gpio_put(APP_DEVICE_RESET_PIN, 1);
    gpio_set_dir(APP_DEVICE_RESET_PIN, GPIO_OUT);    

    // Reset sys device ( Active low )
    gpio_put(APP_DEVICE_RESET_PIN, 0);
    sleep_ms(1);
    gpio_put(APP_DEVICE_RESET_PIN, 1);             
    sleep_ms(1);
}


void picocom_hw_led_set(int state)
{
    gpio_put(PICO_DEFAULT_LED_PIN, state);
}

uint32_t picocom_time_ms_32()
{
    return time_us_64() / 1000;
}

uint32_t picocom_time_us_32()
{
    return time_us_32();
}


uint64_t picocom_time_us_64()
{
    return time_us_64();
}

uint64_t picocom_time_ms_64()
{
    return time_us_64() / 10000;
}


void picocom_sleep_us(uint32_t time)
{    
    sleep_us(time);
}


void picocom_sleep_ms(uint32_t time)
{
    sleep_ms(time);
}

int picocom_message_box(const char* title, const char* msg)
{
    // TODO: render msg box with HID input
    printf("messagebox %s, %s\n", title, msg);
    return 0;
}

void picocom_multicore_launch_core1(Core1Callback_t entry)
{
    multicore_launch_core1(entry);
}
