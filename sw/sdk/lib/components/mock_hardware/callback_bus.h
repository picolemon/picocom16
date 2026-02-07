/** Blocking bus impl
- implements pico bus as blocking callbacks for use in single threaded sim
*/
#include "picocom/platform.h"
#include "lib/platform/pico/bus/bus.h"
#include "lib/components/mock_hardware/pio.h"
#include "picocom/utils/array.h"


/* Router state*/
typedef struct busMockRouter_t
{
    const char* name;        
    bool running;
    struct heap_array_t pios;       // pios mapped (pio.id is the lookup index)
} bus_mock_t;


/* Core mgr state*/
typedef struct coreManager_t
{        
} coreManager_t;


// bus mocking apu
int bus_mock_link_pio(struct busMockRouter_t* router, PIO* tx, PIO* rx);

// core helpers (ideally in its own unit)
int core_manager_create(struct coreManager_t* mgr); // create core manager
int bus_mock_router_create(struct busMockRouter_t* router, int threaded);
void bus_tick_cores_hack();