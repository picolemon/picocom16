#pragma once

#include "picocom/devkit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Scanline tile group of x pixels
 */
typedef struct ScanlineTile_t
{
    
} ScanlineBlock_t;


/** Feeder state */
typedef struct ScanlineFeeder_t
{
    uint8_t colorDepth;     // [EColorDepth_*]
} ScanlineFeeder_t;


// scanline api
int scanlinefeeder_init(struct ScanlineFeeder_t* feeder, bool palleteMode);
int scanlinefeeder_set_palette(struct ScanlineFeeder_t* feeder, const uint8_t* data, uint32_t sz);    // Set scanline pal
int scanlinefeeder_write_scanline_8bpp( struct ScanlineFeeder_t* feeder, const uint8_t* buffer, uint32_t sz, uint32_t line );    // write scanline
int scanlinefeeder_write_scanline_16bpp( struct ScanlineFeeder_t* feeder, const uint16_t* buffer, uint32_t sz, uint32_t line );    // write scanline
uint32_t scanlinefeeder_calc_cmdlistallocaize( bool paletteMode );    // helper for DisplayOptions_t to calc sizes

#ifdef __cplusplus
}
#endif
