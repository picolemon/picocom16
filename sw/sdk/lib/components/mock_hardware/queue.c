#include <picocom/devkit.h>
#include "queue.h"
#include <stdlib.h>
#include <assert.h>

#ifdef PICOCOM_NATIVE_SIM

//
//
static inline void *element_ptr(queue_t *q, uint32_t index) {
    assert(index <= q->element_count);
    return q->data + index * q->element_size;
}

static inline uint16_t inc_index(queue_t *q, uint16_t index) {
    if (++index > q->element_count) { // > because we have element_count + 1 elements
        index = 0;
    }

#if PICO_QUEUE_MAX_LEVEL
    uint16_t level = queue_get_level_unsafe(q);
    if (level > q->max_level) {
        q->max_level = level;
    }
#endif

    return index;
}

uint32_t queue_get_level_unsafe(queue_t *q) {
    int32_t rc = (int32_t)q->wptr - (int32_t)q->rptr;
    if (rc < 0) {
        rc += q->element_count + 1;
    }
    return (uint32_t)rc;
}

bool queue_add_internal(queue_t *q, const void *data, bool block) {
    do {        
        if (queue_get_level_unsafe(q) != q->element_count) {
            memcpy(element_ptr(q, q->wptr), data, q->element_size);
            q->wptr = inc_index(q, q->wptr);

            return true;
        }
        
        if (block) {
            picocom_panic(SDKErr_Fail, "blocking not supported");
            // no block
        } else {
            return false;
        }
    } while (true);
}


static bool queue_remove_internal(queue_t *q, void *data, bool block) {
    do {
        if (queue_get_level_unsafe(q) != 0) {
            if (data) {
                memcpy(data, element_ptr(q, q->rptr), q->element_size);
            }
            q->rptr = inc_index(q, q->rptr);
            return true;
        }
        if (block) {
            picocom_panic(SDKErr_Fail, "blocking not supported");
            // no block
        } else {
            return false;
        }
    } while (true);
}


//
//
void queue_init(queue_t *q, uint32_t element_size, uint32_t element_count)
{
    q->data = (uint8_t *)calloc(element_count + 1, element_size);
    q->element_count = (uint16_t)element_count;
    q->element_size = (uint16_t)element_size;
    q->wptr = 0;
    q->rptr = 0;
}


void queue_free(queue_t *q)
{
    free(q->data);
}


bool queue_try_add(queue_t *q, const void *data)
{
    return queue_add_internal(q, data, false);
}


bool queue_try_remove(queue_t *q, void *data)
{
    return queue_remove_internal(q, data, false);
}


void queue_add_blocking(queue_t *q, const void *data) 
{
    queue_add_internal(q, data, true);
}


void queue_remove_blocking(queue_t *q, void *data) {
    queue_remove_internal(q, data, true);
}


uint32_t queue_get_level(queue_t *q)
{    
    uint32_t level = queue_get_level_unsafe(q);    
    return level;
}

#else

//
//
static inline void *element_ptr(queue_t *q, uint index) {
    assert(index <= q->element_count);
    return q->data + index * q->element_size;
}

static inline uint16_t inc_index(queue_t *q, uint16_t index) {
    if (++index > q->element_count) { // > because we have element_count + 1 elements
        index = 0;
    }

#if PICO_QUEUE_MAX_LEVEL
    uint16_t level = queue_get_level_unsafe(q);
    if (level > q->max_level) {
        q->max_level = level;
    }
#endif

    return index;
}

uint32_t queue_get_level_unsafe(queue_t *q) {
    int32_t rc = (int32_t)q->wptr - (int32_t)q->rptr;
    if (rc < 0) {
        rc += q->element_count + 1;
    }
    return (uint32_t)rc;
}

bool queue_add_internal(queue_t *q, const void *data, bool block) {
    do {
        pthread_mutex_lock(&q->mutex);
        if (queue_get_level_unsafe(q) != q->element_count) {
            memcpy(element_ptr(q, q->wptr), data, q->element_size);
            q->wptr = inc_index(q, q->wptr);
            pthread_mutex_unlock(&q->mutex);

            pthread_cond_signal(&q->cond);
            return true;
        }
        
        if (block) {
            pthread_cond_wait(&q->cond, &q->mutex);
            pthread_mutex_unlock(&q->mutex);
        } else {
            pthread_mutex_unlock(&q->mutex);
            return false;
        }
    } while (true);
}


static bool queue_remove_internal(queue_t *q, void *data, bool block) {
    do {
        pthread_mutex_lock(&q->mutex);
        if (queue_get_level_unsafe(q) != 0) {
            if (data) {
                memcpy(data, element_ptr(q, q->rptr), q->element_size);
            }
            q->rptr = inc_index(q, q->rptr);
            pthread_mutex_unlock(&q->mutex);

            pthread_cond_signal(&q->cond);
            return true;
        }
        if (block) {
            pthread_cond_wait(&q->cond, &q->mutex);       
            pthread_mutex_unlock(&q->mutex);     
        } else {
            pthread_mutex_unlock(&q->mutex);
            return false;
        }
    } while (true);
}


//
//
void queue_init(queue_t *q, uint32_t element_size, uint32_t element_count)
{
    pthread_cond_init(&q->cond, NULL);
    pthread_mutex_init(&q->mutex, NULL);
    q->data = (uint8_t *)calloc(element_count + 1, element_size);
    q->element_count = (uint16_t)element_count;
    q->element_size = (uint16_t)element_size;
    q->wptr = 0;
    q->rptr = 0;
}


void queue_free(queue_t *q)
{
    free(q->data);
}


bool queue_try_add(queue_t *q, const void *data)
{
    return queue_add_internal(q, data, false);
}


bool queue_try_remove(queue_t *q, void *data)
{
    return queue_remove_internal(q, data, false);
}


void queue_add_blocking(queue_t *q, const void *data) 
{
    queue_add_internal(q, data, true);
}


void queue_remove_blocking(queue_t *q, void *data) {
    queue_remove_internal(q, data, true);
}


uint32_t queue_get_level(queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    uint level = queue_get_level_unsafe(q);
    pthread_mutex_unlock(&q->mutex);
    return level;
}
#endif
