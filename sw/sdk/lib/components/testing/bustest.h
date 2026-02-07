#include "picocom/devkit.h"

#define BUSTEST_TX_INTERVAL 1000.0
#define BUSTEST_RX_INTERVAL 100.0
#define BUSTEST_RX_SAMPLES_VALID 8
#define BUSTEST_TX_INIT_FLASH 5000.0

/** Bus tester */
typedef struct busTest_t
{
	uint32_t txPins[32];		// gpio tx pins
	uint32_t txPinCnt;
	uint32_t txAckPin;			// in

	uint32_t rxPins[32];
	uint32_t rxPinCnt;
	uint32_t rxAckPin;			// out

	uint32_t txCnt[32];
	uint32_t rxCnt[32];
	uint32_t rxAckState;

	// tx state
	uint32_t txState;
	uint32_t currentTxPin;
	uint32_t lastRxSetTime;
	uint32_t lastRxSampleValAck;
	uint32_t lastRxSampleHoldCntAck;
	uint32_t rxCntAck;

    const char* testName;

    // rx state
    uint32_t lastRxSummaryTime;

    uint32_t lastRxSampleTime;
    uint32_t lastRxSampleVal[32];
    uint32_t lastRxSampleHoldCnt[32];

    bool passed;
} busTest_t;


void bustest_init( const char* testName, struct busTest_t* bt, uint32_t txCnt, uint32_t rxCnt );
void bustest_set_tx( struct busTest_t* bt, uint32_t bit, uint32_t gpio );
void bustest_set_tx_ack( struct busTest_t* bt, uint32_t gpio );
void bustest_set_rx( struct busTest_t* bt, uint32_t bit, uint32_t gpio );
void bustest_set_rx_ack( struct busTest_t* bt, uint32_t gpio );
void bustest_update_tx( struct busTest_t* bt ) ;
void bustest_update_rx( struct busTest_t* bt );
void bustest_print_summary( struct busTest_t* bt );