add_library(game INTERFACE)
target_sources(game INTERFACE	
	${CMAKE_CURRENT_LIST_DIR}/main.c
)

target_link_libraries(game INTERFACE
	picocom
)

target_include_directories(game INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/.
	# pico compat
	${CMAKE_CURRENT_LIST_DIR}/.
)
