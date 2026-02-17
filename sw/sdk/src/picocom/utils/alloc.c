#include "alloc.h"
#include "stdio.h"
#include <stdlib.h>
#include <malloc.h>


#ifdef PICOCOM_SDL
static __thread struct memAllocState_t allocState;       // per thread state
#else
static struct memAllocState_t allocState;       // per thread state
#endif


void mem_init(int profile, const char* name)
{
    allocState.profileEnabled = profile;    
    allocState.threadId = name ? name : "G";
}


void mem_trigger_min_size(int minSize)
{
    allocState.minSizeTrigger = minSize;
}


void* mem_alloc(size_t size)
{
    if(allocState.profileEnabled)
    {
        allocState.totalAllocCnt++;

        if(allocState.minSizeTrigger && size >= allocState.minSizeTrigger)
        {
           // printf("[%s] alloc_min_size_trigger: %ld (%ld)\n",  allocState.threadId, size, allocState.totalAlloc);
            allocState.minSizeTrigger = allocState.minSizeTrigger;
        }
        uint8_t* mem = malloc(size + sizeof(memHeader_t));
        struct memHeader_t* header = (struct memHeader_t*)mem;
        header->magic = 0xf00f;
        header->size = size;
        header->flags = 1;
        allocState.totalAlloc += header->size;

        printf("[%s] alloc %ld (%ld)\n",  allocState.threadId, size, allocState.totalAlloc);
        if(allocState.totalAlloc > 250*1024)
            allocState.totalAlloc = allocState.totalAlloc;
        return mem + sizeof(memHeader_t);
    }
    else 
    {
        return malloc(size);
    }
}


void* mem_realloc(void * ptr, size_t size)
{
    if(allocState.profileEnabled)
    {
        allocState.totalReallocCnt++;

        // get prefix header
        uint8_t* mem = (uint8_t*)ptr;
        mem -= sizeof(memHeader_t);
        struct memHeader_t* header = (struct memHeader_t*)mem;
        if(header->magic  != 0xf00f) 
        {
            printf("[%s] corrupt mem block %hhn\n", allocState.threadId, mem);
        }

        allocState.totalAlloc -= header->size;

        // realloc
        mem = realloc(mem, size + sizeof(memHeader_t));
        header = (struct memHeader_t*)mem;
        header->magic = 0xf00f;
        header->size = size;
        header->flags = 1;

        allocState.totalAlloc += header->size;

        return mem + sizeof(memHeader_t); 
    }
    else 
    {
        return realloc(ptr, size);
    }
}


void mem_free(void* ptr)
{
    if(allocState.profileEnabled)
    {
        allocState.totalFreeCnt++;

        // get prefix header
        uint8_t* mem = (uint8_t*)ptr;
        mem -= sizeof(memHeader_t);
        struct memHeader_t* header = (struct memHeader_t*)mem;

        if(header->magic  != 0xf00f) 
        {
            printf("[%s] corrupt mem block %hhn\n",  allocState.threadId, mem);
        }

        allocState.totalAlloc -= header->size;

        printf("[%s] free %ld (%ld)\n",  allocState.threadId, header->size, allocState.totalAlloc);

        free(mem);
    }
    else 
    {
        free(ptr);
    }
}


void mem_print_stats_if_different()
{
    if(!allocState.profileEnabled)
        return;

    static size_t totalAllocCnt = 0;
    static size_t totalFreeCnt = 0;
    static size_t totalReallocCnt = 0;
    static size_t totalAlloc = 0;
    static size_t minSizeTrigger = 0;

    if( totalAlloc != allocState.totalAlloc || totalAllocCnt != allocState.totalAllocCnt || totalReallocCnt != allocState.totalReallocCnt || totalFreeCnt != allocState.totalFreeCnt )
    {
        totalAllocCnt = allocState.totalAllocCnt;
        totalFreeCnt = allocState.totalFreeCnt;
        totalReallocCnt = allocState.totalReallocCnt;
        totalAlloc = allocState.totalAlloc;
        minSizeTrigger = allocState.minSizeTrigger;
                
        printf("[%s] memstats total: %ld, stat allocs: %ld, reallocs:%ld, frees:%ld \n",  allocState.threadId, allocState.totalAlloc, allocState.totalAllocCnt, allocState.totalReallocCnt, allocState.totalFreeCnt);
    }
}


void mem_print_stats()
{
    if(!allocState.profileEnabled)
        return;

    printf("[%s] memstats total: %ld, stat allocs: %ld, reallocs:%ld, frees:%ld \n",  allocState.threadId, allocState.totalAlloc, allocState.totalAllocCnt, allocState.totalReallocCnt, allocState.totalFreeCnt);
}


void mem_begin_profile(const char* name)
{
    if(!allocState.profileEnabled)
        return;
    if(allocState.profileName)
        return;

    allocState.profileName = name;
    allocState.profileStartMem = allocState.totalAlloc;
}


size_t mem_end_profile(const char* name)
{
    if(!allocState.profileEnabled)
        return 0;
    if(!allocState.profileName)
        return 0;
    if(allocState.profileName != name)
        return 0;
    printf("[%s] mem_end_profile %s, total: %ld\n", allocState.threadId, allocState.profileName, allocState.totalAlloc - allocState.profileStartMem );
    return 1;
}


// sdk wrappers
#ifdef PICOCOM_MALLOC_IMPL
void* picocom_malloc(size_t sz)
{
    return mem_alloc(sz);
}


void picocom_free(void* ptr)
{
    mem_free(ptr);
}


void* picocom_relloc(void * ptr, size_t size)
{
    return mem_realloc(ptr, size);
}

#else

void* picocom_malloc(size_t sz)
{
    return malloc(sz);
}


void picocom_free(void* ptr)
{
    free(ptr);
}


void* picocom_relloc(void * ptr, size_t size)
{
    return realloc(ptr, size);
}
#endif


void mem_begin_tag(uint32_t tagBit)
{
    allocState.tagBits |= tagBit;
}


void mem_end_tag(uint32_t tagBit)
{
    allocState.tagBits &= ~(1 << tagBit); 
}


uint32_t mem_total_size()
{
    return allocState.totalAlloc;
}


uint32_t picocom_mem_free()
{
#ifdef PICOCOM_SDL    
    return 0;
#else
    struct mallinfo m = mallinfo();
    extern char __StackLimit, __bss_end__;
    int totalHeap = &__StackLimit  - &__bss_end__;
    int totalFree = totalHeap - m.uordblks;
    return totalFree;
#endif
}
