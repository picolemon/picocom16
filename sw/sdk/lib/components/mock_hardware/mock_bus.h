/** Pico bus mock runtime api
 * Usage:
 *  - include pico sdk bus lib/platform/pico/bus/bus.h & define PICOCOM_SDL
 */
#include "picocom/platform.h"
#include "lib/platform/pico/bus/bus.h"
#include "lib/components/mock_hardware/pio.h"
#include "picocom/utils/array.h"
#include <pthread.h>

/** Mock transport cmd */
enum EBusMockCmd
{
    EBusMockCmd_None,
    EBusMockCmd_busMockTXBusTransmitEvent,
    EBusMockCmd_busMockRXAckEvent
};


/** Base cmd wrapper */
typedef struct busMockCmd_t {
    uint32_t cmd; // cmd
    uint32_t size;  // allocated size    
    void* data;     // malloced data (rx must free)    
} busMockCmd_t;


/** Gpio RX ack event */
typedef struct busMockRXAckEvent_t {
    struct busMockCmd_t header;
    uint32_t id;
} busMockRXAckEvent_t;


/** Gpio TX transmit event */
typedef struct busMockTXBusTransmitEvent_t {
    struct busMockCmd_t header;

} busMockTXBusTransmitEvent_t;


/* Router state*/
typedef struct busMockRouter_t
{
    const char* name;
    pthread_mutex_t mutex;
    pthread_t thread_tx;
    pthread_t thread_rx;
    int threaded;
    bool running;
    struct heap_array_t pios;       // pios mapped (pio.id is the lookup index)
} bus_mock_t;

typedef void (*CoreManagerMainCallback_t)(void);


/* Core mgr thread*/
typedef struct coreThread_t
{
    pthread_t thread;    
    CoreManagerMainCallback_t entry;
} coreThread_t;


/* Core mgr state*/
typedef struct coreManager_t
{
    pthread_mutex_t mutex;
    struct heap_array_t threads;  // [coreThread_t] threads
} coreManager_t;


// bus mocking apu
int bus_mock_link_pio(struct busMockRouter_t* router, PIO* tx, PIO* rx);


// core helpers (ideally in its own unit)
int core_manager_create(struct coreManager_t* mgr); // create core manager
int core_manager_launch(struct coreManager_t* mgr, CoreManagerMainCallback_t entry); // create core manager
int core_manager_join(struct coreManager_t* mgr);
int bus_mock_router_create(struct busMockRouter_t* router, int threaded);