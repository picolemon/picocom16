/** FatFS glue for sdl sim
 */
#pragma once

#include "picocom/platform.h"
#include "picocom/utils/array.h"


/** Mounted SDCard image state 
*/
typedef struct SDLDiskImageMount_t
{
    char mountedFilename[0xff];    
    uint8_t* image; 
    uint32_t imageSz;
    bool readonly;
    bool mounted;
} SDLDiskImageMount_t;


// storage sim api
int storage_mount_image(const char* filename, bool readonly);       // Mount virtual sdcard image, copy writable in mem copy 
void storage_unmount_image();                                       // Remove virtual sdcard from slot
bool storage_has_mount();
int storage_save_image(const char* filename);
bool storage_has_image(const char* filename);
bool storage_set_dir(const char* filename);
const char* storage_get_dir();
bool storage_create_native_dir(const char* filename);               // Native save dir used with storage_set_dir to create base mount data dir
int storage_post_save();