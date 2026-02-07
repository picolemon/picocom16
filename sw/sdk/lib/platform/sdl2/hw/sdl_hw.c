#include "platform/sdl2/sdl_platform.h"
#include "picocom/devkit.h"
#include <pthread.h>


// Globals
pthread_t g_core1_thread;


//
//
void picocom_hw_setup_clocks(PicocomInitOptions_t* options)
{
    // no hw clocks
}

void picocom_hw_init(PicocomInitOptions_t* options)
{
    // no hw
}


void picocom_sleep_us(uint32_t time)
{
    SDL_Delay(time/1000); // Timing on non low level devices much harder with os context switching.
}


void picocom_sleep_ms(uint32_t time)
{
    SDL_Delay(time);
}


void picocom_hw_led_set(int state)
{
    // no hw led
}


uint32_t picocom_time_us_32()
{
    return SDL_GetTicks() * 1000;
}


uint32_t picocom_time_ms_32()
{
    return SDL_GetTicks() ;
}


uint64_t picocom_time_us_64()
{
    return SDL_GetTicks() * 1000;
}


int picocom_message_box(const char* title, const char* msg)
{    
	const SDL_MessageBoxButtonData buttons[] = {
		{ 0, 0, "Cancel" },
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Confirm" },
	};

	const SDL_MessageBoxData messageboxdata = {
		SDL_MESSAGEBOX_INFORMATION,
		NULL,
		title,
		msg,
		SDL_arraysize(buttons),
		buttons,
		NULL
	};
	int buttonid;
	if (SDL_ShowMessageBox(&messageboxdata, &buttonid) < 0) {
		SDL_Log("error displaying message box");
		return SDKErr_OK;
	}

    return buttonid;
}

#ifndef PICOCOM_NATIVE_SIM

static Core1Callback_t g_nextCallback = 0;
void* core1_wrapper(void*)
{
	g_nextCallback();
	return 0;
}

void picocom_multicore_launch_core1(Core1Callback_t callback)
{    
	g_nextCallback = callback;
	pthread_create(&g_core1_thread, NULL, core1_wrapper, 0);
}

#endif