# SPDX-License-Identifier: MIT
#
# Application build (CMake): the FreeRTOS S-mode demo. The CMake counterpart
# of the Makefile in this directory, and a template for new applications.
#
# It only uses the MpfsSmode:: targets: from an installed copy of the
# libraries (find_package; built with make or CMake), or from the library
# build when the project's root CMakeLists.txt adds both.
#
#   cmake -S examples/freertos-demo -B build-demo \
#         -DCMAKE_TOOLCHAIN_FILE=<prefix>/lib/cmake/MpfsSmode/riscv64-unknown-elf.cmake \
#         -DCMAKE_PREFIX_PATH=<prefix>
#   cmake --build build-demo
#
# FreeRTOSConfig.h: the configuration is compiled into libfreertos_smode, and
# "FreeRTOS.h" includes the copy installed next to it. The one in this
# directory only takes effect if the libraries are built with it
# (FREERTOS_CONFIG_DIR, which the project's root CMakeLists.txt passes).

cmake_minimum_required(VERSION 3.24)

if(NOT TARGET MpfsSmode::mpfs_smode)
    project(freertos_demo LANGUAGES C ASM)
    find_package(MpfsSmode REQUIRED)
endif()

add_executable(freertos-demo
    app_main.c
    m_prehook.c
    other_harts.c
    ddr_buffers.c
    periph_selftest.c
    spi_flash.c)

set_target_properties(freertos-demo PROPERTIES SUFFIX ".elf")
target_compile_options(freertos-demo PRIVATE -Wall -Wextra)

# The demo's layout of the DDR window (g_ddr_buffer_0..2): an implicit linker
# script, added to the library's mpfs-lim-sv39.ld after the objects.
target_link_libraries(freertos-demo PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ddr_buffers.ld"
    MpfsSmode::mpfs_smode
    MpfsSmode::mpfs_drivers)
set_property(TARGET freertos-demo APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ddr_buffers.ld")
target_link_options(freertos-demo PRIVATE
    "-Wl,-Map=$<TARGET_FILE_DIR:freertos-demo>/freertos-demo.map")

add_custom_command(TARGET freertos-demo POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:freertos-demo>
            $<TARGET_FILE_DIR:freertos-demo>/freertos-demo.hex
    COMMAND ${CMAKE_SIZE} --format=sysv --radix=16 $<TARGET_FILE:freertos-demo>
    VERBATIM)
