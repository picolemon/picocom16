#include "array.h"


//
//
int heap_array_init(struct heap_array_t* array, uint32_t elemSz, uint32_t capacity)
{
    if(!array)
        return 0;
    memset(array, 0, sizeof(struct heap_array_t));
    array->allocSize = elemSz*capacity;
    array->elemSize = elemSz;
    array->capacity = capacity;
    array->count = 0;
    if(array->allocSize)
        array->buffer = picocom_malloc(array->allocSize);
    return 1;
}


int heap_array_resize(struct heap_array_t* array, uint32_t elemSz, uint32_t capacity)
{
    if(!array)
        return 0;
    
    array->allocSize = elemSz*capacity;
    array->elemSize = elemSz;
    array->capacity = capacity;    
    if(array->allocSize)
    {
        array->buffer = array->buffer ? picocom_relloc(array->buffer, array->allocSize) :  picocom_malloc(array->allocSize);
    }
    else if(array->buffer)
    {
        picocom_free(array->buffer);
        array->buffer = 0;
    }
        
    return array->buffer != 0; // true on buffer, clearing will return 0
}


void heap_array_memset_capacity(struct heap_array_t* array)
{
    if(!array || !array->allocSize)
        return;

    memset(array->buffer, 0, array->allocSize);
}



int heap_array_set_count(struct heap_array_t* array, uint32_t count)
{
    if(count > array->capacity)
        return 0;
    
    array->count = count;

    return 1;
}


void heap_array_deinit(struct heap_array_t* array)
{
    if(!array)
        return;
    if(array->buffer)
        picocom_free(array->buffer);
    memset(array, 0, sizeof(struct heap_array_t));
}


uint8_t* heap_array_get_ptr(struct heap_array_t* array, uint32_t i)
{
    if(!array)
        return 0;
    if(i >= array->count)
        return 0;  
    return array->buffer + (array->elemSize * i);
}


int heap_array_set(struct heap_array_t* array, uint32_t i, uint8_t* ptr, uint32_t sz)
{
    if(!array || !ptr)
        return 0;
    if(sz > array->elemSize)
        return 0;
    memcpy( array->buffer + (array->elemSize * i), ptr, sz );
    return 1;
}


int heap_array_append(struct heap_array_t* array, uint8_t* ptr, uint32_t sz)
{
    // ensure size
    if(array->count >= array->capacity)
    {
        // inc size by 1 elem
        if(!heap_array_resize(array, array->elemSize, array->capacity + 1))
            return 0;
    }

    uint32_t i = array->count;
    int res = heap_array_set(array, i, ptr, sz);
    if(!res)
        return 0;
    array->count++;

    return 1;
}

int heap_array_remove_and_swap(struct heap_array_t* array, uint32_t index)
{
    if(index >= array->count || !array->count)
        return 0; // invalid index

    // check if end
    if(index == array->count-1)
    {
        // pop end
        array->count--;
        return 1;
    }
    else 
    {    
        // copy end to index slot
        uint8_t* src = heap_array_get_ptr(array, array->count - 1); // last
        uint8_t* dst = heap_array_get_ptr(array, index); // last -> index
        memcpy(dst, src, array->elemSize);

        // pop end
        array->count--;
        return 1;
    }
}


int heap_array_set_uint32(struct heap_array_t* array, uint32_t i, uint32_t v)
{
    return heap_array_set(array, i, (uint8_t*)&v, sizeof(v));
}


uint32_t heap_array_get_uint32(struct heap_array_t* array, uint32_t i)
{
    uint32_t* ptr = (uint32_t*)heap_array_get_ptr(array, i);
    if(!ptr)
        return 0;
    return *ptr;
}


int heap_array_get_value(struct heap_array_t* array, uint32_t i, uint8_t* value, uint32_t sz)
{
    uint8_t* ptr = heap_array_get_ptr(array, i);
    if(!ptr)
        return 0;
    if(sz != array->elemSize)
        return 0;
    memcpy(value, ptr, sz);
    return 1;
}


uint32_t heap_array_pop_back_uint32(struct heap_array_t* array)
{
    if(array->count <= 0)
        return 0;
    
    // get last
    uint32_t result = heap_array_get_uint32(array, array->count - 1);

    // dec
    array->count--;

    return result;
}


int heap_array_push_back_uint32(struct heap_array_t* array, uint32_t v)
{
    if(array->count >= array->capacity)
        return 0;

    // inc and alloc new slot
    uint32_t prevCnt = array->count;
    array->count++;
    
    // set last
    if(!heap_array_set_uint32(array, array->count - 1, v))
    {
        // restore and error
        array->count = prevCnt; 
        return 0;
    }

    return 1;
}


int heap_array_remove_uint32(struct heap_array_t* array, uint32_t v)
{
    int iterCnt = 0;
    bool changes = false;
    do
    {
        changes = false;
        for(int i=0;i<array->count;i++)
        {
            uint32_t checkV = heap_array_get_uint32(array, i);
            if(v == checkV)
            {
                // remove elem at index
                heap_array_remove_and_swap(array, i );

                // iter again until all matches removed, could just continue but this could mutate array and just repeat to be safe
                iterCnt++;
                changes = true; 
                break;
            }
        }
    } while( changes );
    return iterCnt;
}


int heap_array_clone(struct heap_array_t* src, struct heap_array_t* dst)
{
    if(!src || !dst)
        return 0;
    
    // ensure dst free
    if(dst->buffer)
        picocom_free(dst->buffer);
    memcpy(dst, src, sizeof(struct heap_array_t));
    
    // alloc new
    dst->buffer = picocom_malloc(src->allocSize);

    // copy data
    memcpy(dst->buffer, src->buffer, src->capacity);

    return 1;
}


//
//
int heap_circular_array_init(struct heap_circular_array_t* array, uint32_t elemSz, uint32_t capacity)
{
    if(!array)
        return 0;
    memset(array, 0, sizeof(struct heap_circular_array_t));
    array->allocSize = elemSz*capacity;
    array->elemSize = elemSz;
    array->capacity = capacity;
    array->count = 0;
    if(array->allocSize)
        array->buffer = picocom_malloc(array->allocSize);
    array->start = 0;
    array->end = 0;
    return 1;    
}


int heap_circular_array_is_full( struct heap_circular_array_t* array )
{
    return array->count == array->capacity;
}


int heap_circular_array_is_empty( struct heap_circular_array_t* array )
{
    return array->count == 0;
}


int heap_circular_array_add_first( struct heap_circular_array_t* array, uint8_t* value, uint32_t sz )
{
    if( sz != array->elemSize )
        return 0;
    if( heap_circular_array_is_full( array ))
        return 0;

    array->start = (array->start - 1 + array->capacity) % array->capacity;

    uint8_t* startPtr = array->buffer + (array->elemSize * array->start);
    memcpy(startPtr, value, sz);    
    array->count++;        

    return 1;
}


int heap_circular_array_remove_first( struct heap_circular_array_t* array )
{
    if( heap_circular_array_is_empty( array ))
        return 0;

    array->start = (array->start + 1) % array->capacity;
    array->count--;

    return 1;
}

int heap_circular_array_add_last( struct heap_circular_array_t* array, uint8_t* value, uint32_t sz )
{
    if( sz != array->elemSize )
        return 0;
    if( heap_circular_array_is_full( array ))
        return 0;

    uint8_t* endPtr = array->buffer + (array->elemSize * array->end);
    memcpy(endPtr, value, sz);    
    array->end = (array->end + 1) % array->capacity;
    array->count++;

    return 1;
}


int heap_circular_array_remove_last( struct heap_circular_array_t* array )
{    
    if( heap_circular_array_is_empty( array ))
        return 0;
    
    array->end = (array->end - 1 + array->capacity) % array->capacity;    
    array->count--;

    return 1;
}


uint8_t* heap_circular_array_get_first_ptr( struct heap_circular_array_t* array )
{
    uint8_t* startPtr = array->buffer + (array->elemSize * array->start);
    return startPtr;
}


uint8_t* heap_circular_array_get_last_ptr( struct heap_circular_array_t* array )
{
    uint8_t* endPtr = array->buffer + (array->elemSize * array->end);
    return endPtr;
}


uint8_t* heap_circular_array_get_at_ptr( struct heap_circular_array_t* array, uint32_t index )
{
    if( index >= array->count )
        return 0;
    uint8_t* startPtr = array->buffer + (array->elemSize * (array->start + index));
    return startPtr;
}