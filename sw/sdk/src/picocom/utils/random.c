#include "random.h"


//
//
void pseudo_random_init(struct pseudo_random_t* rng)
{
    rng->XVec = 49304862;
    rng->YVec = 59863158;
}


void pseudo_random_set_seed(struct pseudo_random_t* rng, uint32_t u, uint32_t v)
{
	rng->XVec = u;
	rng->YVec = v;
}


void pseudo_random_offset_seed(struct pseudo_random_t* rng, uint32_t u, uint32_t v)
{
    rng->XVec += u;
	rng->YVec += v;
}


uint32_t pseudo_random_uint(struct pseudo_random_t* rng)
{
    rng->YVec = 36969 * (rng->YVec & 65535) + (rng->YVec >> 16);
	rng->XVec = 18000 * (rng->XVec & 65535) + (rng->XVec >> 16);
	return (rng->YVec << 16) + rng->XVec;
}


uint32_t pseudo_random_uint_range(struct pseudo_random_t* rng, uint32_t start, uint32_t end)
{
	uint32_t u = pseudo_random_uint(rng);	
    uint32_t range = end - start;
	return start + (u % range);
}
