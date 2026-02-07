#pragma once
#include <stdint.h>
#include "hardware/structs/systick.h"
#include "hardware/timer.h"


//
//
#define BEGIN_PROFILE_BLOCK_CYCLES() \
    systick_hw->csr = 0x5; \
    systick_hw->rvr = 0x00FFFFFF; \
    uint32_t newt, old; \
    old=systick_hw->cvr; \


#define END_PROFILE_BLOCK_CYCLES() \
    newt=systick_hw->cvr; \


#define BEGIN_PROFILE_BLOCK_US() \
    uint32_t t0, t1; \
    t0=time_us_32(); \


#define END_PROFILE_BLOCK_US() \
    t1=time_us_32(); \


#define PROFILE_REPORT_US() \
    printf("took: %d us (%d ms)\n",t1-t0, (t1-t0)/1000); \


#define PROFILE_REPORT_CYC() \
    printf("took: %d cycles\n",old-newt); \


// Full profile
#define BEGIN_PROFILE() \
    BEGIN_PROFILE_BLOCK_CYCLES(); \
    BEGIN_PROFILE_BLOCK_US(); \

#define END_PROFILE() \
    END_PROFILE_BLOCK_CYCLES(); \
    END_PROFILE_BLOCK_US(); \
    PROFILE_REPORT_US(); \
    PROFILE_REPORT_CYC(); \

    