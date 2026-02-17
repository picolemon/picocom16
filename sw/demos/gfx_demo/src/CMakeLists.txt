add_library(game INTERFACE)
target_sources(game INTERFACE	
	${CMAKE_CURRENT_LIST_DIR}/main.c
	${CMAKE_CURRENT_LIST_DIR}/demo.c
	${CMAKE_CURRENT_LIST_DIR}/simple_demos.c
	${CMAKE_CURRENT_LIST_DIR}/complex_demos.c
	${CMAKE_CURRENT_LIST_DIR}/custom_gfx.c	
)

target_link_libraries(game INTERFACE
	picocom
)

target_include_directories(game INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
