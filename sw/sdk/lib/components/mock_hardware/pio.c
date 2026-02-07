#include "pio.h"
#include "mock_bus.h"
#include <stdlib.h>


//
//
PIO pio_mock_create()
{
    PIO pio = (PIO)malloc(sizeof(PIO_t));
    memset(pio, 0, sizeof(PIO_t));

#ifndef PICOCOM_NATIVE_SIM    
    queue_init(&pio->tx_out_queue, sizeof(busMockCmd_t), 1024);
    queue_init(&pio->rx_ack_out_queue, sizeof(busMockCmd_t), 1024);
#endif

    return pio;
}