#pragma GCC optimize ("O0")
#include "lib/components/testing/bustest.h"
#include <stdio.h>
#include <hardware/gpio.h>


//
//
void bustest_init( const char* testName, struct busTest_t* bt, uint32_t txCnt, uint32_t rxCnt )
{
	memset( bt, 0, sizeof(*bt));
	bt->txPinCnt = txCnt + 2;	// Assume  + clk + cs
	bt->rxPinCnt = rxCnt + 2; 	// Assume  + clk + cs
    bt->testName = testName;
    bt->lastRxSummaryTime = picocom_time_ms_32();
    printf("[%s] init\n", bt->testName);
    picocom_hw_init_led_only(false); 
}


void bustest_set_tx( struct busTest_t* bt, uint32_t bit, uint32_t gpio )
{
	bt->txPins[ bit ] = gpio;

	gpio_init(gpio);
    gpio_put(gpio, 0);
	gpio_set_dir(gpio, GPIO_OUT);    
}


void bustest_set_tx_ack( struct busTest_t* bt, uint32_t gpio )
{
	bt->txAckPin = gpio;

	gpio_init(gpio);    
	gpio_set_dir(gpio, GPIO_IN);    
}


void bustest_set_rx( struct busTest_t* bt, uint32_t bit, uint32_t gpio )
{
	bt->rxPins[ bit ] = gpio;

	gpio_init(gpio);    
	gpio_set_dir(gpio, GPIO_IN);    	
}


void bustest_set_rx_ack( struct busTest_t* bt, uint32_t gpio )
{
	bt->rxAckPin = gpio;

	gpio_init(gpio);    
	gpio_put(gpio, 0);
	gpio_set_dir(gpio, GPIO_OUT);       		
}


void bustest_update_tx( struct busTest_t* bt ) 
{
	// Write each tx bit in sequence at an interval
	if( bt->txState == 0 )
	{
		/*if( picocom_time_ms_32() - bt->lastRxSetTime > BUSTEST_TX_INIT_FLASH)
		{
			for(int i=0;i<bt->txPinCnt;i++)
			{
				gpio_put( bt->txPins[i], 0 );			
			}			

			bt->txState = 1;
		}
		else
		{
			for(int i=0;i<bt->txPinCnt;i++)
			{
				gpio_put( bt->txPins[i], 1 );			
			}			
		}*/		
        bt->txState = 1;
	}
	else if( bt->txState == 1 )
	{
		if( picocom_time_ms_32() - bt->lastRxSetTime > BUSTEST_TX_INTERVAL)
		{
			bt->lastRxSetTime = picocom_time_ms_32();

			printf("tx put gpio: %d\n", bt->txPins[bt->currentTxPin] )        ;

			for(int i=0;i<bt->txPinCnt;i++)
			{        				
				int gpio = bt->txPins[i];
				int val = (i == bt->currentTxPin) ? 1 : 0;
				gpio_put( gpio, val );			
			}

			bt->currentTxPin++;
			if( bt->currentTxPin >= bt->txPinCnt )
				bt->currentTxPin = 0;

			// toggle rx ack pin
			bt->rxAckState = !bt->rxAckState;
			gpio_put( bt->rxAckPin, bt->rxAckState );	
		}
	}
}


void bustest_update_rx( struct busTest_t* bt )
{
	if( picocom_time_ms_32() - bt->lastRxSampleTime > BUSTEST_RX_INTERVAL )
    {
        bt->lastRxSampleTime = picocom_time_ms_32();

        // sample gpio    
        for(int i=0;i<bt->rxPinCnt;i++)
        {                
            int v = gpio_get( bt->rxPins[i] );

            if( bt->lastRxSampleVal[i] == v )
            {
                bt->lastRxSampleHoldCnt[i] += 1; 
            }
            else
            {
                bt->lastRxSampleVal[i] = v;
                bt->lastRxSampleHoldCnt[i] = 1; 
            }    

            // Sample cycle ( positive )
            if(  bt->lastRxSampleHoldCnt[i] > BUSTEST_RX_SAMPLES_VALID && v)
            {
                bt->lastRxSampleVal[i] = v;
                bt->lastRxSampleHoldCnt[i] = 1; 
                bt->rxCnt[i]++;
            }
        }


        // sample ack            
        {                
            int v = gpio_get( bt->txAckPin );

            if( bt->lastRxSampleValAck == v )
            {
                bt->lastRxSampleHoldCntAck += 1; 
            }
            else
            {
                bt->lastRxSampleValAck = v;
                bt->lastRxSampleHoldCntAck = 1; 
            }    

            // Sample cycle ( positive )
            if(  bt->lastRxSampleHoldCntAck > 8 && v)
            {
                bt->lastRxSampleValAck = v;
                bt->lastRxSampleHoldCntAck = 1; 
                bt->rxCntAck++;
            }
        }


        // Check all passed

        int validCycleCnt = 0;
        for(int i=0;i<bt->rxPinCnt;i++)
            if(bt->rxCnt[i] > 1)
                validCycleCnt++;
        if( bt->rxCntAck > 1 )
            validCycleCnt++;

        if( validCycleCnt >= bt->rxPinCnt + 1) // bus + ack
        {
            // pass
            bt->passed = 1;
            picocom_hw_led_set(1);
        }
    }
}

void bustest_print_summary( struct busTest_t* bt )
{
    if( picocom_time_ms_32() - bt->lastRxSummaryTime > 1000 )
    {
        bt->lastRxSummaryTime = picocom_time_ms_32();
        printf("[%s] summary - %s\n", bt->testName, bt->passed ? "[PASSED]" : "[IN PROGRESS]" );

        for(int i=0;i<bt->rxPinCnt;i++)
        {
            printf("\tinput[%d] gpio: %d holdSamples: %d, state: %d, cycles: %d\n", i, bt->rxPins[i], bt->lastRxSampleHoldCnt[i], bt->lastRxSampleVal[i], bt->rxCnt[i])      ;
        }

		printf("\ttxAckPin[ACK] gpio: %d holdSamples: %d, state: %d, cycles: %d\n", bt->txAckPin, bt->lastRxSampleHoldCntAck, bt->lastRxSampleValAck, bt->rxCntAck)      ;
			
    }
}

