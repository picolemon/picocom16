# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

project(input_demo C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# game source
add_subdirectory( ../../src         build/gamebuild)

# piccom sdk
add_subdirectory( ../../deps/sdk       build/sdkbuild)

add_executable(input_demo)

pico_set_program_name(input_demo "input_demo")
pico_set_program_version(input_demo "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(input_demo 1)
pico_enable_stdio_usb(input_demo 0)

# Add the standard library to the build
target_link_libraries(input_demo
        game
        picocom
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(input_demo PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

pico_add_extra_outputs(input_demo)

