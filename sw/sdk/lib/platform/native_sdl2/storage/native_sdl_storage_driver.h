#pragma once
#include "picocom/platform.h"
#include "picocom/utils/array.h"

// storage sim api
int storage_mount_image(const char* filename, bool readonly);       // Mount virtual sdcard image, copy writable in mem copy 
void storage_unmount_image();                                       // Remove virtual sdcard from slot
bool storage_has_mount();
int storage_save_image(const char* filename);
bool storage_has_image(const char* filename);
bool storage_set_dir(const char* filename);
int storage_post_save();