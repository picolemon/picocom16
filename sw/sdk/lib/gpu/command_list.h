#pragma once

#include "gpu_types.h"


//
// command list
struct GpuCommandList_t* gpu_cmd_list_init(uint32_t allocSz, uint32_t headerSz);  // Init command list for size (in bytes)
void gpu_cmd_list_clear(struct GpuCommandList_t* list);                           // clear cmd buffer
int gpu_cmd_list_add(struct GpuCommandList_t* list, GpuCmd_Header* gpuCmd);  // Add command to buffer (copies)
GpuCmd_Header* gpu_cmd_list_add_next(struct GpuCommandList_t* list, uint32_t sz); // Add command & alloc next cmd
int gpu_cmd_list_can_add(struct GpuCommandList_t* list, uint32_t sz); // Returns 1 if enough room

// command list list
struct GpuCommandListList_t* gpu_cmd_list_list_init(uint32_t allocSz, uint32_t headerSz, uint32_t maxCmdListCnt );  // Init command list for size (in bytes)
void gpu_cmd_list_list_clear(struct GpuCommandListList_t* list);                           // clear cmd buffer
int gpu_cmd_list_list_add(struct GpuCommandListList_t* list, GpuCmd_Header* gpuCmd);      // Add command to buffer (copies)
GpuCmd_Header* gpu_cmd_list_list_add_next(struct GpuCommandListList_t* list, uint32_t sz); // Add command & alloc next cmd
int gpu_cmd_list_list_get_count(struct GpuCommandListList_t* list);      // Get command list count
GpuCommandList_t* gpu_cmd_list_list_add_list(struct GpuCommandListList_t* listlist);            // Add new cmd list
GpuCommandList_t* gpu_cmd_list_list_get(struct GpuCommandListList_t* listlist, uint32_t index);            // Get list at index
GpuCommandList_t* gpu_cmd_list_list_get_current(struct GpuCommandListList_t* listlist);            // Get top most list

// cmd list double buffering
int gpu_buffer_double_buffer_init(GpuCommandListListDoubleBuffer* doubleBufferList, uint32_t allocSz, uint32_t headerSz, uint32_t maxCmdListCnt, int groupId ); // Init double buffer
struct GpuCommandListList_t* gpu_buffer_get_writing(GpuCommandListListDoubleBuffer* doubleBufferList);    // Get current writing buffer
struct GpuCommandListList_t* gpu_buffer_get_reading(GpuCommandListListDoubleBuffer* doubleBufferList);    // Get reading/rendering buffer
void gpu_buffer_flip(GpuCommandListListDoubleBuffer* doubleBufferList);           // Flip buffers 


