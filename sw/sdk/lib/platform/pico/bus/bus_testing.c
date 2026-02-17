#pragma GCC optimize ("O0")
#include "bus_testing.h"
#include "stdio.h"


//
//
uint32_t pio_read_reg(PIO pio, uint32_t sm, enum pio_src_dest reg) 
{
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_mov(pio_isr, reg));
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_push(false, false));
    return pio_sm_get(pio, sm);
}

uint32_t pio_write_reg(PIO pio, uint32_t sm, enum pio_src_dest reg, uint32_t value)
{
    pio_sm_put(pio, sm, value);
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_pull(false, false)); // pull -> ISR
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_mov(pio_x, pio_osr)); // mov Y, OSR
    return pio_read_reg(pio, sm, reg);
}


bool check_interval( uint32_t* lastTimePtr, float inverval )
{
    uint32_t now = time_us_32(); 
    uint32_t lastTime = *lastTimePtr;

    float deltaSec = (now-lastTime) / 1000000.0f;
    if( deltaSec > inverval )
    {
        *lastTimePtr = now;
        return true;
    }
    else 
    {
        return false;
    }    
}


void bus_rx_print_stats(const char* busname, BusRx_t* bus_rx)
{
    struct Res_Bus_Diag_Stats stats = {};
    bus_rx_update_stats(bus_rx, &stats); 
    printf("[%s] rx(%f b/s, %f MB/s), scnt:%d, enproc:%d, ehdr: %d%s, ackcnt: %d\n", busname, stats.busRateRx, stats.busRateRx/1000000.0f, 
        bus_rx->rx_success_cnt,
        bus_rx->rx_pendingCmdNotProcessedErrCnt, bus_rx->rx_invalidHeaderCnt, bus_rx->rx_in_irq ? "*":"", bus_rx->rx_ack_cnt);
}


void bus_tx_print_stats(const char* busname, BusTx_t* bus_tx)
{
    struct Res_Bus_Diag_Stats stats = {};
    bus_tx_update_stats(bus_tx, &stats); 
    printf("[%s] tx(%f b/s, %f MB/s), ak(%d->%d) txcnt:%d, ack_cnt:%d, err:%d\n", 
        busname, 
        stats.busRateTx, 
        stats.busRateTx/1000000.0f,
        bus_tx->rx_pending_ack_cnt, 
        bus_tx->rx_ack_cnt,
        bus_tx->tx_total_send_cmd_cnt, 
        bus_tx->rx_ack_cnt,
        stats.busErrorsTx
    );
}


float bus_tx_stats_last_time( BusTx_t* bus_tx)
{
    uint32_t t1=time_us_32(); 
    double deltaS = (t1-bus_tx->last_total_tx_time)/1000000.0f;
    return deltaS;
}


float bus_rx_stats_last_time( BusRx_t* bus_rx)
{
    uint32_t t1=time_us_32(); 
    double deltaS = (t1-bus_rx->last_total_rx_time)/1000000.0f;
    return deltaS;
}
