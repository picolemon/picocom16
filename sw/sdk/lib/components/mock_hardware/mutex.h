#pragma once

#include "picocom/platform.h"
#include <pthread.h>

typedef struct {
} recursive_mutex_t;

typedef struct mutex {
    pthread_mutex_t mutex;
} mutex_t;

void mutex_init(mutex_t *mtx);
void mutex_enter_blocking(mutex_t *mtx);
void mutex_exit(mutex_t *mtx);