cmake_minimum_required(VERSION 3.5.0)
set(CMAKE_CXX_STANDARD 17)
project(gfx_demo VERSION 0.1.0)
set(OpenGL_GL_PREFERENCE "GLVND")
set(PICOCOM_SDL 1)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# game source
add_subdirectory( ../../src         build/gamebuild)
# piccom sdk
add_subdirectory( ../../deps/sdk       build/sdkbuild)

# Executable and link
if (NOT WIN32)
    string(STRIP ${SDL2_LIBRARIES} SDL2_LIBRARIES)
endif (NOT WIN32)

add_executable(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} game picocom ${SDL2_LIBRARIES} m asound)
