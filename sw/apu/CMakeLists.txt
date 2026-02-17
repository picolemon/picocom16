# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable SDK apu impl
set(PICOCOM_APU_IMPL 1)

# Enable remote SD bridge
add_compile_definitions(PICO_DEOPTIMIZED_DEBUG=1)
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
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(apu C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory( ${CMAKE_CURRENT_LIST_DIR}/../sdk/thirdparty/pico_audio_i2s build1 )
add_subdirectory( ${CMAKE_CURRENT_LIST_DIR}/../sdk/thirdparty/pico_audio  build2)
add_subdirectory( ${CMAKE_CURRENT_LIST_DIR}/../sdk/thirdparty/pico_util_buffer build3)

add_subdirectory( ../sdk build )

add_executable(apu src/apu.c  
        src/hid_app.c
        src/hw_config.c
    )

pico_set_program_name(apu "apu")
pico_set_program_version(apu "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(apu 1) # uses 2 irqs
pico_enable_stdio_usb(apu 0)

target_compile_definitions(apu PRIVATE
    PICO_AUDIO_I2S_MONO_INPUT=0
    USE_AUDIO_I2S=1
    PICO_AUDIO_I2S_DATA_PIN=9
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
    PICO_AUDIO_I2S_PIO=2
)

# Add the standard library to the build
target_link_libraries(apu 
        picocom
        hardware_pio
        pico_stdlib
        pico_audio_i2s
        tinyusb_additions
        tinyusb_host 
        tinyusb_board 
        FatFs_SPI
        )

# Add the standard include files to the build
target_include_directories(apu PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

pico_add_extra_outputs(apu)

# Linker
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS "-Wl,--print-memory-usage")

#if(${PICO_PLATFORM} STREQUAL "rp2040")
  #pico_set_linker_script(${CMAKE_PROJECT_NAME} ${CMAKE_SOURCE_DIR}/../sdk/lib/platform/pico/linker/memmap_default_rp2040.ld)
#elseif(${PICO_PLATFORM} MATCHES "rp2350")
  #pico_set_linker_script(${CMAKE_PROJECT_NAME} ${CMAKE_SOURCE_DIR}/../sdk/lib/platform/pico/linker/memmap_default_rp2350.ld)
#endif()
