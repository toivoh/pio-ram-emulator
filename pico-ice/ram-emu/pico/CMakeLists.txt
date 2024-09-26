cmake_minimum_required(VERSION 3.13)

# import the pico-sdk
set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk/)
include(pico_sdk_import.cmake)

# configure the pico-sdk project
project(pio-ram-emulator C CXX ASM)
pico_sdk_init()

# add the pico-ice-sdk
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/pico-ice-sdk/)

# add the local files
add_executable(${CMAKE_PROJECT_NAME}
	ram-emu-main.c
	usb_descriptors.c
	../../../ram-emu.c
	../../../ram-emu.h
	)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/../../../sram_memmap.ld)
pico_add_link_depend(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/../../../sram_memmap.ld)

target_link_libraries(${CMAKE_PROJECT_NAME}
	pico_ice_sdk
	pico_ice_usb
	pico_stdio_usb
	hardware_pwm
	)
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
	)
pico_add_extra_outputs(${CMAKE_PROJECT_NAME})
pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 0)
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 0)

pico_generate_pio_header(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/../../../serial-ram-emu.pio)
