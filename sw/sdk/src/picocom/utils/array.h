#pragma once

#include "picocom/platform.h"

/** Array type */
struct heap_array_t
{    
    uint8_t* buffer;
    uint32_t allocSize;     // Array alloc size
    uint32_t elemSize;      // Elem size
    uint32_t capacity;      // Total elem capacity
    uint32_t count;         // Current count
};

/** Circular array type */
struct heap_circular_array_t
{    
    uint8_t* buffer;
    uint32_t allocSize;     // Array alloc size
    uint32_t elemSize;      // Elem size
    uint32_t capacity;      // Total elem capacity
    uint32_t count;         // Current count
    int start;
    int end;
};


// heap array
int heap_array_init(struct heap_array_t* array, uint32_t elemSz, uint32_t capacity);
int heap_array_resize(struct heap_array_t* array, uint32_t elemSz, uint32_t capacity);
void heap_array_deinit(struct heap_array_t* array);
void heap_array_memset_capacity(struct heap_array_t* array); //
int heap_array_set_count(struct heap_array_t* array, uint32_t count);
uint8_t* heap_array_get_ptr(struct heap_array_t* array, uint32_t i);
int heap_array_get_value(struct heap_array_t* array, uint32_t i, uint8_t* value, uint32_t sz);
int heap_array_set(struct heap_array_t* array, uint32_t i, uint8_t* ptr, uint32_t sz);
int heap_array_append(struct heap_array_t* array, uint8_t* ptr, uint32_t sz);
int heap_array_remove_and_swap(struct heap_array_t* array, uint32_t index); // remove at index and swap with last elem (to avoid reallow, array reordered as side effect)
uint32_t heap_array_get_uint32(struct heap_array_t* array, uint32_t i);
int heap_array_remove_uint32(struct heap_array_t* array, uint32_t v);
int heap_array_push_back_uint32(struct heap_array_t* array, uint32_t v);
uint32_t heap_array_pop_back_uint32(struct heap_array_t* array);
int heap_array_set_uint32(struct heap_array_t* array, uint32_t i, uint32_t v);
int heap_array_clone(struct heap_array_t* src, struct heap_array_t* dst);

// circular
int heap_circular_array_init( struct heap_circular_array_t* array, uint32_t elemSz, uint32_t capacity );
int heap_circular_array_is_full( struct heap_circular_array_t* array );
int heap_circular_array_is_empty( struct heap_circular_array_t* array );
int heap_circular_array_add_first( struct heap_circular_array_t* array, uint8_t* value, uint32_t sz );
int heap_circular_array_remove_first( struct heap_circular_array_t* array );
int heap_circular_array_add_last( struct heap_circular_array_t* array, uint8_t* value, uint32_t sz );
int heap_circular_array_remove_last( struct heap_circular_array_t* array );
uint8_t* heap_circular_array_get_first_ptr( struct heap_circular_array_t* array );
uint8_t* heap_circular_array_get_last_ptr( struct heap_circular_array_t* array );
uint8_t* heap_circular_array_get_at_ptr( struct heap_circular_array_t* array, uint32_t index ); // Get at offset from start until count
