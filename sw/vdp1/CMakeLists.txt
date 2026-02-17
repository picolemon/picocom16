# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0") 
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
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

project(vdp1 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_subdirectory(../sdk build)

add_executable(vdp1
    src/vdp1.c    
)


# pull in common dependencies
target_link_libraries(
    vdp1 	  
    pico_multicore
    hardware_pio    
    pico_util    
    hardware_dma    
    hardware_flash
    pico_flash
    pico_stdlib
    pico_base_headers
    picocom
  )

if (PICO_CYW43_SUPPORTED)
    target_link_libraries(vdp1 pico_cyw43_arch_none)
endif()

# create map/bin/hex file etc.
pico_add_extra_outputs(vdp1)

# add url via pico_set_program_url
include_directories(	
	include	  
)

# Linker
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS "-Wl,--print-memory-usage")

#if(${PICO_PLATFORM} STREQUAL "rp2040")
  #pico_set_linker_script(${CMAKE_PROJECT_NAME} ${CMAKE_SOURCE_DIR}/../sdk/lib/platform/pico/linker/memmap_default_rp2040.ld)
#elseif(${PICO_PLATFORM} MATCHES "rp2350")
  #pico_set_linker_script(${CMAKE_PROJECT_NAME} ${CMAKE_SOURCE_DIR}/../sdk/lib/platform/pico/linker/memmap_default_rp2350.ld)
#endif()
