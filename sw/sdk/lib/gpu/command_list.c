#include "picocom/devkit.h"
#include <stdint.h>
#include <string.h>
#include "command_list.h"
#include "gpu.h"


//
//
struct GpuCommandList_t* gpu_cmd_list_init(uint32_t allocSz, uint32_t headerSz)
{
    struct GpuCommandList_t* list = (struct GpuCommandList_t*)gpu_malloc(sizeof(GpuCommandList_t));
    if(!list)
        return 0;
    memset(list, 0, sizeof(GpuCommandList_t));

    list->allocSz = allocSz;
    list->headerSz = headerSz;

    // alloc buffer
    list->cmdData = gpu_malloc(allocSz+headerSz);
    if(!list->cmdData)
        return 0;
    memset(list->cmdData, 0, allocSz+headerSz);

    gpu_cmd_list_clear(list);    
    return list;
}


void gpu_cmd_list_clear(struct GpuCommandList_t* list)
{    
    list->offset = list->headerSz;    // Start at end of header
    list->cmdCount = 0;
    list->cmdCount = 0;
}


int gpu_cmd_list_add(struct GpuCommandList_t* list, GpuCmd_Header* gpuCmd)
{
    GpuCmd_Header* dstCmd = gpu_cmd_list_add_next(list,  gpuCmd->sz);
    if(!dstCmd)
        return 0;
    memcpy(dstCmd, gpuCmd, gpuCmd->sz );    
    return 1;
}


GpuCmd_Header* gpu_cmd_list_add_next(struct GpuCommandList_t* list, uint32_t sz)
{
    GpuCmd_Header* result = (GpuCmd_Header*)(list->cmdData + list->offset);

    if(!sz)
        return 0;

    // Check alloc overflow
    if( list->offset + sz > list->allocSz )
        return 0;

    list->offset += sz;
    list->cmdCount++;

    return result;
}


int gpu_cmd_list_can_add(struct GpuCommandList_t* list, uint32_t sz)
{
    if(!sz)
        return 0;
    if( list->offset + sz >= list->allocSz )
        return 0;
    return 1;
}


//
//
struct GpuCommandListList_t* gpu_cmd_list_list_init(uint32_t allocSz, uint32_t headerSz, uint32_t maxCmdListCnt )
{
    if(!allocSz)
        picocom_panic(SDKErr_Fail, "!allocSz");
    if(!maxCmdListCnt)
        picocom_panic(SDKErr_Fail, "!maxCmdListCnt");    
    GpuCommandListList_t* listlist = (GpuCommandListList_t*)gpu_malloc( sizeof(GpuCommandListList_t) );
    memset(listlist, 0, sizeof(GpuCommandListList_t));

    listlist->lists = (GpuCommandList_t**)gpu_malloc( sizeof(GpuCommandList_t*) * maxCmdListCnt );
    for(int i=0;i<maxCmdListCnt;i++)
    {
        listlist->lists[i] = (struct GpuCommandList_t*)gpu_cmd_list_init( allocSz, headerSz );
    }
    listlist->listAllocCnt = maxCmdListCnt;
    listlist->offset = 0;
    
    return listlist;
}


void gpu_cmd_list_list_clear(struct GpuCommandListList_t* listlist)
{
    listlist->offset = 0;
    for(int i=0;i<listlist->listAllocCnt;i++)
    {
        gpu_cmd_list_clear(listlist->lists[i]);
    }

    listlist->submitId = 0;
    listlist->completeFlags = 0;    
}


int gpu_cmd_list_list_add(struct GpuCommandListList_t* listlist, GpuCmd_Header* gpuCmd)
{
    GpuCmd_Header* cmd = gpu_cmd_list_list_add_next(listlist, gpuCmd->sz);
    if(!cmd)
        return 0;
    memcpy(cmd, gpuCmd, gpuCmd->sz);
    return 1;
}


GpuCommandList_t* gpu_cmd_list_list_add_list(struct GpuCommandListList_t* listlist)
{
    // add new list
    listlist->offset++;
    if(listlist->offset >= listlist->listAllocCnt)
        return 0;
    GpuCommandList_t * list = listlist->lists[listlist->offset];
    if(!list)
        return 0;        
    gpu_cmd_list_clear(list);

    return list;
}


GpuCmd_Header* gpu_cmd_list_list_add_next(struct GpuCommandListList_t* listlist, uint32_t sz)
{
    if(listlist->offset >= listlist->listAllocCnt)
        return 0;

    // Get current list
    GpuCommandList_t* list = listlist->lists[listlist->offset];
    if(!list)
        return 0;

    // check room
    if(!gpu_cmd_list_can_add(list, sz))
    {
        // add new list
        listlist->offset++;
        if(listlist->offset >= listlist->listAllocCnt)
            return 0;
        list = listlist->lists[listlist->offset];
        if(!list)
            return 0;        
        gpu_cmd_list_clear(list);
    }

    // try add to list
    return gpu_cmd_list_add_next(list, sz);
}

int gpu_cmd_list_list_get_count(struct GpuCommandListList_t* list)
{
    int listCnt = list->offset+1;
    if(listCnt > list->listAllocCnt)
        return list->listAllocCnt;

    return listCnt;
}


GpuCommandList_t* gpu_cmd_list_list_get(struct GpuCommandListList_t* listlist, uint32_t index)
{
    if(index >= listlist->listAllocCnt)
        return 0;
    if(index > listlist->offset) // 1 based eg. offset==0 & index ==0 valid
        return 0;
    return listlist->lists[index];
}


//
//
int gpu_buffer_double_buffer_init(GpuCommandListListDoubleBuffer* doubleBufferList, uint32_t allocSz, uint32_t headerSz, uint32_t maxCmdListCnt, int groupId )
{
    if(maxCmdListCnt < 1)
        picocom_panic(SDKErr_Fail, "maxCmdListCnt < 1");
    
    memset(doubleBufferList, 0, sizeof(GpuCommandListListDoubleBuffer));

    for(int i=0;i<2;i++)
    {
        doubleBufferList->buffers[i] = gpu_cmd_list_list_init(allocSz, headerSz, maxCmdListCnt );
        if(!doubleBufferList->buffers[i])
            return 0;        
    }

    return 1;
}


struct GpuCommandListList_t* gpu_buffer_get_writing(GpuCommandListListDoubleBuffer* doubleBufferList)
{
    int index = (doubleBufferList->currentBufferId + 0) % 2;
    if(index >= 2)
        picocom_panic(SDKErr_Fail, "Invalid index");
    return doubleBufferList->buffers[ index ];
}


struct GpuCommandListList_t* gpu_buffer_get_reading(GpuCommandListListDoubleBuffer* doubleBufferList)
{
    int index = (doubleBufferList->currentBufferId + 1) % 2;
    if(index >= 2)
        picocom_panic(SDKErr_Fail, "Invalid index");
    return doubleBufferList->buffers[ index ];
}


void gpu_buffer_flip(GpuCommandListListDoubleBuffer* doubleBufferList)
{
    // Ensure not locked
    struct GpuCommandListList_t* writing = gpu_buffer_get_writing(doubleBufferList);
    struct GpuCommandListList_t* reading = gpu_buffer_get_reading(doubleBufferList);

    // Ensure correct timing of pipeline, should never flip when writing
    if(writing->accessType != EGpuCmdListAccessType_None && writing->accessType != EGpuCmdListAccessType_Write)
        picocom_panic(SDKErr_Fail, "!writer list access type invalid");
    if(reading->accessType != EGpuCmdListAccessType_None && reading->accessType != EGpuCmdListAccessType_ReadShared)
        picocom_panic(SDKErr_Fail, "!reader list access type invalid");

    // flip
    doubleBufferList->currentBufferId++;

    // re-get
    writing = gpu_buffer_get_writing(doubleBufferList);
    reading = gpu_buffer_get_reading(doubleBufferList);    

    // Lock next reader for reading
    reading->accessType = EGpuCmdListAccessType_ReadShared;
    writing->accessType = EGpuCmdListAccessType_Write;
}


GpuCommandList_t* gpu_cmd_list_list_get_current(struct GpuCommandListList_t* listlist)
{
    if(!listlist)
        return 0;
    return gpu_cmd_list_list_get(listlist, listlist->offset);    
}
