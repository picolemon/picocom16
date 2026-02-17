add_library(picocom INTERFACE)

if(PICO_SDK)
	add_compile_definitions(PICOCOM_PICO)
endif()
if(PICOCOM_SDL)
	add_compile_definitions(PICOCOM_SDL)

	# Enable SDK apu impl with sim
	set(PICOCOM_APU_IMPL 1)
endif()

# Thirdpaty sdks
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/thirdparty/no-OS-FatFS-SD-SPI-RPi-Pico-local/FatFs_SPI build2)		# Fat file system support

target_sources(picocom INTERFACE	
	# sdk
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/devkit.c
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/display/display.c
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/display/gfx.c
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/audio/audio.c
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/storage/storage.c
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/input/input.c
	# soft gpu
	${CMAKE_CURRENT_LIST_DIR}/lib/gpu/gpu.c
	${CMAKE_CURRENT_LIST_DIR}/lib/gpu/command_list.c		
	${CMAKE_CURRENT_LIST_DIR}/lib/gpu/gpu_cmd_impl.c	
	${CMAKE_CURRENT_LIST_DIR}/lib/gpu/gpu_3d.c	
	${CMAKE_CURRENT_LIST_DIR}/lib/gpu/gpu_3d_impl.cpp
	# vdp client
	${CMAKE_CURRENT_LIST_DIR}/lib/components/vdp1_core/vdp_client.c	
	${CMAKE_CURRENT_LIST_DIR}/lib/components/vdp1_core/scanline.c	
	# apu client
	${CMAKE_CURRENT_LIST_DIR}/lib/components/apu_core/apu_client.c	
	# vdp impl
	${CMAKE_CURRENT_LIST_DIR}/lib/components/vdp1_core/vdp1_core.c	
	${CMAKE_CURRENT_LIST_DIR}/lib/components/vdp2_core/vdp2_core.c			
	# utils
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/utils/array.c
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/utils/random.c
	${CMAKE_CURRENT_LIST_DIR}/src/picocom/utils/alloc.c
	# flash
	${CMAKE_CURRENT_LIST_DIR}/lib/components/flash_store/flash_store.c
	# thirdparty
	${CMAKE_CURRENT_LIST_DIR}/thirdparty/crc16/crc.c
	${CMAKE_CURRENT_LIST_DIR}/thirdparty/miniz/miniz.c	
)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/thirdparty/tgx)	

if(PICOCOM_APU_IMPL)
	target_sources(picocom INTERFACE	
		# apu impl
		${CMAKE_CURRENT_LIST_DIR}/lib/components/apu_core/apu_core.c
		${CMAKE_CURRENT_LIST_DIR}/lib/components/apu_core/audio_engine.c
		${CMAKE_CURRENT_LIST_DIR}/lib/components/apu_core/minivorbis.c	
	)
endif() # PICOCOM_APU_IMPL

target_include_directories(picocom INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src
	${CMAKE_CURRENT_LIST_DIR}/lib	
	${CMAKE_CURRENT_LIST_DIR}/
	${CMAKE_CURRENT_LIST_DIR}/thirdparty
	${CMAKE_CURRENT_LIST_DIR}/..
)


if(PICOCOM_SDL)

	if(PICOCOM_NATIVE_SIM)

		# Single threaded simple api level 		
		target_sources(picocom INTERFACE	
			# shared
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/hw/sdl_hw.c
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/mutex.c		
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/queue.c	
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/input/sdl_input_driver.c
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/pio.c
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/audio/sdl_audio_driver.c			
			# pico platform
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/native_sdl2/native_sdl_platform.c					
			# storage
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/native_sdl2/storage/native_sdl_storage_driver.c
			# hw
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/callback_bus.c	
			# vdp sim
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/display/sdl_display_driver.c
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/display/test_core_vdp1.c
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/display/test_core_vdp2.c	
			# apu sim
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/audio/test_core_apu.c		
		)

		target_link_libraries(picocom INTERFACE					
			tgx
		)		
	else()

		# Threaded simulation 
		target_sources(picocom INTERFACE	
			# pico platform
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/sdl_platform.c
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/hw/sdl_hw.c
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/audio/sdl_audio_driver.c
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/input/sdl_input_driver.c
			# mock hw
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/stdio.c
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/pio.c	
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/queue.c	
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/mock_bus.c	
			${CMAKE_CURRENT_LIST_DIR}/lib/components/mock_hardware/mutex.c	
			# vdp sim
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/display/sdl_display_driver.c
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/display/test_core_vdp1.c
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/display/test_core_vdp2.c
			# apu sim
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/audio/test_core_apu.c
			# storage 
			${CMAKE_CURRENT_LIST_DIR}/lib/platform/sdl2/storage/sdl_storage_driver.c
		)

		target_link_libraries(picocom INTERFACE		
			FatFs_SPI
			tgx
		)
	endif()

endif() # PICOCOM_SDL

if(PICO_SDK)
	target_sources(picocom INTERFACE	
		# pico platform
		${CMAKE_CURRENT_LIST_DIR}/lib/platform/pico/hw/picocom_hw.c
		${CMAKE_CURRENT_LIST_DIR}/lib/platform/pico/bus/bus.c
		${CMAKE_CURRENT_LIST_DIR}/lib/platform/pico/bus/bus_testing.c					
		${CMAKE_CURRENT_LIST_DIR}/thirdparty/crc16/crc.c
		${CMAKE_CURRENT_LIST_DIR}/lib/platform/pico/boot/boot.c			
		${CMAKE_CURRENT_LIST_DIR}/lib/components/testing/bustest.c
	)

	target_link_libraries(picocom INTERFACE
		pico_multicore
		pico_stdlib
		hardware_pio
		hardware_dma
		hardware_flash
		FatFs_SPI
		tgx
	)

	pico_generate_pio_header(picocom ${CMAKE_CURRENT_LIST_DIR}/lib/platform/pico/bus/bus_tx.pio)
	pico_generate_pio_header(picocom ${CMAKE_CURRENT_LIST_DIR}/lib/platform/pico/bus/bus_rx.pio)
endif() # PICO_SDK
