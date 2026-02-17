#pragma once

#include "picocom/platform.h"

/** Random generator state
 */
typedef struct pseudo_random_t
{
	unsigned int XVec;
	uint32_t YVec;
} pseudo_random_t;

// randomg gen api
void pseudo_random_init(struct pseudo_random_t* rng); // init rng with default seed
void pseudo_random_set_seed(struct pseudo_random_t* rng, uint32_t u, uint32_t v); // set seed
void pseudo_random_offset_seed(struct pseudo_random_t* rng, uint32_t u, uint32_t v); // offset seed
uint32_t pseudo_random_uint(struct pseudo_random_t* rng); // random uint
double pseudo_random_normal(struct pseudo_random_t* rng); // random  0-1
uint32_t pseudo_random_uint_range(struct pseudo_random_t* rng, uint32_t start, uint32_t end); // Random uint
int pseudo_random_int_range(struct pseudo_random_t* rng, int start, int end); // Random int
float pseudo_random_float(struct pseudo_random_t* rng, float start, float end); // Random float