#pragma once

#include "picocom/platform.h"

// Config
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
//#define DISPLAY_NULL

// Display driver api
void display_driver_setup_clocks(); // init system clocks for dvi timings
bool display_driver_init(); // init display
uint16_t* get_display_buffer(); // get working back buffer
uint16_t* flip_display_blocking(); // flip display & wait for next back buffer
bool display_driver_get_init();