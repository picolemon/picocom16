#include "mutex.h"

//
//
#if PICOCOM_NATIVE_SIM

// Stub mutex for single thread
void mutex_init(mutex_t *mtx) {}

void mutex_enter_blocking(mutex_t *mtx) {}

void mutex_exit(mutex_t *mtx) {}

#else
void mutex_init(mutex_t *mtx)
{
    pthread_mutex_init(&mtx->mutex, NULL);
}

void mutex_enter_blocking(mutex_t *mtx)
{
    //printf("L %p\n", (void*)mtx);
    pthread_mutex_lock(&mtx->mutex);
}

void mutex_exit(mutex_t *mtx)
{
    //printf("U %p\n", (void*)mtx);
    pthread_mutex_unlock(&mtx->mutex);
}
#endif
