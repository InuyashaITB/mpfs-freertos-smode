# SPDX-License-Identifier: MIT
#
# FreeRTOS S-mode demo, built against the MpfsSmode libraries.
#
# Built automatically as part of the top-level project, or standalone against
# an installed package:
#
#   cmake -S examples/freertos-demo -B build-demo \
#         -DCMAKE_TOOLCHAIN_FILE=<prefix>/lib/cmake/MpfsSmode/riscv64-unknown-elf.cmake \
#         -DCMAKE_PREFIX_PATH=<prefix>
#   cmake --build build-demo
#
# The FreeRTOSConfig.h in this directory is the one the top-level build uses
# for libfreertos_smode by default (FREERTOS_CONFIG_DIR). Against an installed
# package, the copy installed with the library is the one that counts:
# "FreeRTOS.h" includes the FreeRTOSConfig.h next to it.

cmake_minimum_required(VERSION 3.24)

if(NOT TARGET MpfsSmode::mpfs_smode)
    project(freertos_demo LANGUAGES C ASM)
    find_package(MpfsSmode 1.0 REQUIRED)
endif()

add_executable(freertos-demo
    app_main.c
    m_prehook.c
    other_harts.c
    ddr_buffers.c)

set_target_properties(freertos-demo PROPERTIES SUFFIX ".elf")
target_compile_options(freertos-demo PRIVATE -Wall -Wextra)
target_link_options(freertos-demo PRIVATE
    "-Wl,-Map=$<TARGET_FILE_DIR:freertos-demo>/freertos-demo.map")
target_link_libraries(freertos-demo PRIVATE
    MpfsSmode::mpfs_smode
    MpfsSmode::mpfs_drivers)

add_custom_command(TARGET freertos-demo POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:freertos-demo>
            $<TARGET_FILE_DIR:freertos-demo>/freertos-demo.hex
    COMMAND ${CMAKE_SIZE} --format=sysv --radix=16 $<TARGET_FILE:freertos-demo>
    VERBATIM)
