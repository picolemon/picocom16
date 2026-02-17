# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0") # DEBUG/SLOW
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")

add_compile_definitions(PICO_MAX_SHARED_IRQ_HANDLERS=8)
add_compile_definitions(PICOCOM_OVERRIDE_MAIN)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.0.0)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
include(${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(vdp2 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_subdirectory(../sdk build)

add_executable(vdp2
    src/vdp2.c
    src/display_driver.c

    # testing
    #src/testing/test.c
    #src/testing/sprite.c 
    #src/testing/sprite_armv6m.S 
)


# pull in common dependencies
target_link_libraries(
    vdp2 
	pico_stdlib
    pico_multicore
    hardware_pio    
    pico_util    
    hardware_dma
    libdvi 
    m
    picocom)

if (PICO_CYW43_SUPPORTED)
    target_link_libraries(vdp2 pico_cyw43_arch_none)
endif()

# create map/bin/hex file etc.
pico_add_extra_outputs(vdp2)

# add url via pico_set_program_url
include_directories(	
	include	  
)

add_subdirectory(libdvi)

