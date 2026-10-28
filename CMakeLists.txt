# SPDX-License-Identifier: MIT
#
# Bare-bones S-mode FreeRTOS hello-world for PolarFire SoC U54_1: the same
# single ELF as the Makefile.
#
#   cmake -S . -B build-cmake -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/riscv64-unknown-elf.cmake \
#         -DFREERTOS_KERNEL_DIR=<FreeRTOS-Kernel tree>
#   cmake --build build-cmake            -> build-cmake/hello.elf, .hex, .map

cmake_minimum_required(VERSION 3.20)
project(mpfs_smode_hello LANGUAGES C ASM)

set(FREERTOS_KERNEL_DIR "" CACHE PATH "FreeRTOS-Kernel tree (required)")
set(LIBC_SPECS "--specs=nano.specs" CACHE STRING "C library specs (picolibc: --specs=picolibc.specs)")
set(LIBC_LINK_SPECS "--specs=nosys.specs" CACHE STRING "Extra link-only specs (picolibc: empty)")
if(CMAKE_C_COMPILER_VERSION VERSION_LESS 12)
    set(_march rv64gc)
else()
    set(_march rv64gc_zicsr_zifencei)
endif()
set(MARCH "${_march}" CACHE STRING "-march value")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug (-O0 -g3) or Release (-Os -g)" FORCE)
endif()

get_filename_component(KERNEL "${FREERTOS_KERNEL_DIR}" ABSOLUTE)
if(NOT FREERTOS_KERNEL_DIR OR NOT EXISTS "${KERNEL}/tasks.c" OR NOT EXISTS "${KERNEL}/include/FreeRTOS.h")
    message(FATAL_ERROR "Set FREERTOS_KERNEL_DIR to a FreeRTOS-Kernel tree (with tasks.c and include/FreeRTOS.h)")
endif()

add_executable(hello
    boot/entry.S boot/m_trap.S boot/m_shim.c boot/s_start.S
    port/RISC-V-SMode/port.c port/RISC-V-SMode/portASM.S
    app/main.c
    "${KERNEL}/list.c" "${KERNEL}/queue.c" "${KERNEL}/tasks.c"
    "${KERNEL}/portable/MemMang/heap_4.c")
set_target_properties(hello PROPERTIES SUFFIX ".elf")
set_source_files_properties(boot/m_shim.c app/main.c port/RISC-V-SMode/port.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

separate_arguments(_libc NATIVE_COMMAND "${LIBC_SPECS}")
separate_arguments(_libc_link NATIVE_COMMAND "${LIBC_LINK_SPECS}")
set(_arch -march=${MARCH} -mabi=lp64d -mcmodel=medany)
target_include_directories(hello PRIVATE app boot port/RISC-V-SMode "${KERNEL}/include")
target_compile_options(hello PRIVATE ${_arch} -mstrict-align -fno-jump-tables
    -ffunction-sections -fdata-sections -fmessage-length=0 ${_libc})
target_link_options(hello PRIVATE ${_arch} ${_libc} ${_libc_link} -nostartfiles
    -Wl,--gc-sections -T "${CMAKE_CURRENT_SOURCE_DIR}/boot/linker.ld"
    "-Wl,-Map=$<TARGET_FILE_DIR:hello>/hello.map")
set_property(TARGET hello APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/boot/linker.ld")

add_custom_command(TARGET hello POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:hello> $<TARGET_FILE_DIR:hello>/hello.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:hello>
    VERBATIM)
