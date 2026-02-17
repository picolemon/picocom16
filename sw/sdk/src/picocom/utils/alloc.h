#pragma once

#include "picocom/platform.h"

/** Memory profile allocator */
typedef struct memAllocState_t 
{
    const char* threadId;
    int profileEnabled;
    size_t totalAllocCnt;
    size_t totalFreeCnt;
    size_t totalReallocCnt;
    size_t totalAlloc;
    size_t minSizeTrigger;

    const char* profileName;
    size_t profileStartMem;
    uint32_t tagBits;
} memAllocStatee_t;


typedef struct memHeader_t
{
    uint16_t magic;
    uint16_t flags; 
    uint32_t tagBits;
    size_t size;
} memHeader_t;


// Alloc api
void mem_init(int profile, const char* name);
void mem_trigger_min_size(int minSize);
void* mem_alloc(size_t size);
void* mem_realloc(void * ptr, size_t size);
void mem_free(void* ptr);
void mem_print_stats_if_different();
void mem_print_stats();
void mem_begin_profile(const char* name);
size_t mem_end_profile(const char* name);
void mem_begin_tag(uint32_t tagBit);
void mem_end_tag(uint32_t tagBit);
uint32_t mem_total_size();

void* picocom_relloc(void * ptr, size_t size);
void* picocom_malloc(size_t sz);
void picocom_free(void* ptr);
void* picocom_relloc(void * ptr, size_t size);
uint32_t picocom_mem_free();