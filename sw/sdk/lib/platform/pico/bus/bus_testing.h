#pragma once
#include "bus.h"

#ifdef __cplusplus
extern "C" {
#endif


// bus rx profiler
void bustest_start_rx_profiler_core2(int size);  // run profile on core 2
void bustest_profiler_rx_handle_message(Cmd_Header_t* cmdIn); // rx message to profiler for display (valid or not)
void bus_rx_print_stats(const char* busname, BusRx_t* bus_rx);
float bus_rx_stats_last_time( BusRx_t* bus_rx);

// bus tx profiler
void bustest_start_tx_profiler_core2(int size);  // run profile on core 2
void bustest_profiler_tx_handle_message(Cmd_Header_t* cmdOut); // tx message to profiler for display
void bus_tx_print_stats(const char* busname, BusTx_t* bus_tx);
float bus_tx_stats_last_time( BusTx_t* bus_tx);

// test data gen
Cmd_Header_t* bustest_gen_test_message_of_size(int size, uint8_t* buffer); // gen test message with buffer

// pio debug
uint32_t pio_read_reg(PIO pio, uint32_t sm, enum pio_src_dest reg);
uint32_t pio_write_reg(PIO pio, uint32_t sm, enum pio_src_dest reg, uint32_t value);

// Utils
bool check_interval( uint32_t* lastTime, float inverval );

#ifdef __cplusplus
}
#endif
