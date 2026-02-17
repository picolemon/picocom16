#pragma once

#include "picocom/platform.h"
#ifndef PICOCOM_NATIVE_SIM
    #include <pthread.h>
#endif

typedef struct queue_t
{
#ifndef PICOCOM_NATIVE_SIM    
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif    
    uint8_t *data;
    uint16_t wptr;
    uint16_t rptr;
    uint16_t element_size;
    uint16_t element_count;
} queue_t;

// queue api
void queue_init(queue_t *q, uint32_t element_size, uint32_t element_count);
void queue_free(queue_t *q);
bool queue_try_add(queue_t *q, const void *data);
bool queue_try_remove(queue_t *q, void *data);
bool queue_try_peek(queue_t *q, void *data);
void queue_add_blocking(queue_t *q, const void *data);
void queue_remove_blocking(queue_t *q, void *data);
void queue_peek_blocking(queue_t *q, void *data);
uint32_t queue_get_level(queue_t *q);