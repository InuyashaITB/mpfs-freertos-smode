# SPDX-License-Identifier: MIT
#
# Bare-bones S-mode FreeRTOS for PolarFire SoC U54_1, as two static libraries
# and an application that links them (the Makefile builds the same):
#
#   libmpfs_smode_boot.a   boot/: M-mode _start, delegation and PMP, the SBI
#                          shim, the M->S handoff (s_start). No FreeRTOS: from
#                          the application it needs s_mode_main and
#                          mpfs_sv39_root_table, from the port
#                          freertos_risc_v_trap_handler.
#   libfreertos_smode.a    the FreeRTOS kernel (FREERTOS_KERNEL_DIR) and the
#                          S-mode port (port/), built against the
#                          FreeRTOSConfig.h in FREERTOS_CONFIG_DIR
#   MpfsSmode::mpfs_smode  what an application links: both libraries
#                          whole-archive, boot/linker.ld, the flags
#
#   cmake --preset local && cmake --build --preset local     libraries + app/
#   cmake --install build-cmake/local --prefix <prefix>       the package
#
# app/ is built here too (MPFS_SMODE_BUILD_APP, default ON), or on its own
# against an installed package with find_package(MpfsSmode): see
# app/CMakeLists.txt.

cmake_minimum_required(VERSION 3.24)
project(mpfs_smode LANGUAGES C ASM)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/MpfsSmodeFlags.cmake")
mpfs_smode_default_march(_default_march)

set(FREERTOS_KERNEL_DIR "" CACHE PATH "FreeRTOS-Kernel tree (required)")
set(FREERTOS_CONFIG_DIR "config" CACHE PATH
    "Directory with the FreeRTOSConfig.h built into libfreertos_smode (installed with it)")
set(LIBC_SPECS "--specs=nano.specs" CACHE STRING "C library specs (picolibc: --specs=picolibc.specs)")
set(LIBC_LINK_SPECS "--specs=nosys.specs" CACHE STRING "Extra link-only specs (picolibc: empty)")
set(MARCH "${_default_march}" CACHE STRING "-march value")
option(MPFS_SMODE_BUILD_APP "Also build app/ against the libraries of this tree" ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug (-O0 -g3) or Release (-Os -g)" FORCE)
endif()

get_filename_component(KERNEL "${FREERTOS_KERNEL_DIR}" ABSOLUTE)
if(NOT FREERTOS_KERNEL_DIR OR NOT EXISTS "${KERNEL}/tasks.c" OR NOT EXISTS "${KERNEL}/include/FreeRTOS.h")
    message(FATAL_ERROR "Set FREERTOS_KERNEL_DIR to a FreeRTOS-Kernel tree (with tasks.c and include/FreeRTOS.h)")
endif()
get_filename_component(_config_dir "${FREERTOS_CONFIG_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${_config_dir}/FreeRTOSConfig.h")
    message(FATAL_ERROR "FREERTOS_CONFIG_DIR (${_config_dir}) has no FreeRTOSConfig.h")
endif()
set(BOOT "${CMAKE_CURRENT_SOURCE_DIR}/boot")
set(PORT "${CMAKE_CURRENT_SOURCE_DIR}/port/RISC-V-SMode")
set(LDSCRIPT "${BOOT}/linker.ld")

mpfs_smode_flags("${MARCH}" "${LIBC_SPECS}" "${LIBC_LINK_SPECS}" MPFS_CFLAGS MPFS_LDFLAGS)
mpfs_smode_whole_archive_support()

add_library(mpfs_smode_boot STATIC
    "${BOOT}/entry.S" "${BOOT}/m_trap.S" "${BOOT}/m_shim.c" "${BOOT}/s_start.S")
target_include_directories(mpfs_smode_boot PRIVATE "${BOOT}")

add_library(freertos_smode STATIC
    "${PORT}/port.c" "${PORT}/portASM.S"
    "${KERNEL}/list.c" "${KERNEL}/queue.c" "${KERNEL}/tasks.c"
    "${KERNEL}/portable/MemMang/heap_4.c")
target_include_directories(freertos_smode PRIVATE "${_config_dir}" "${KERNEL}/include" "${PORT}")

foreach(_lib mpfs_smode_boot freertos_smode)
    target_compile_options(${_lib} PRIVATE ${MPFS_CFLAGS})
endforeach()
set_source_files_properties("${BOOT}/m_shim.c" "${PORT}/port.c"
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# Application side: headers and flags, then everything to link.
add_library(mpfs_smode_headers INTERFACE)
target_include_directories(mpfs_smode_headers INTERFACE
    "${BOOT}" "${_config_dir}" "${KERNEL}/include" "${PORT}")
target_compile_options(mpfs_smode_headers INTERFACE ${MPFS_CFLAGS})

# Whole-archive: nothing in an application refers to the boot code (the
# bootloader jumps to _start), so a plain archive link would drop it.
add_library(mpfs_smode INTERFACE)
target_link_options(mpfs_smode INTERFACE ${MPFS_LDFLAGS} "-T${LDSCRIPT}")
target_link_libraries(mpfs_smode INTERFACE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,mpfs_smode_boot,freertos_smode>"
    mpfs_smode_headers)
set_property(TARGET mpfs_smode APPEND PROPERTY INTERFACE_LINK_DEPENDS "${LDSCRIPT}")

foreach(_t mpfs_smode_boot freertos_smode mpfs_smode_headers mpfs_smode)
    add_library(MpfsSmode::${_t} ALIAS ${_t})
endforeach()

# The package: the same layout as make's staged build/dist.
install(TARGETS mpfs_smode_boot freertos_smode ARCHIVE DESTINATION lib)
install(FILES "${BOOT}/mpfs.h" DESTINATION include)
install(DIRECTORY "${KERNEL}/include/" DESTINATION include/freertos FILES_MATCHING PATTERN "*.h")
install(FILES "${PORT}/portmacro.h" "${PORT}/portContext.h"
              "${PORT}/freertos_risc_v_chip_specific_extensions.h"
              "${_config_dir}/FreeRTOSConfig.h"
        DESTINATION include/freertos)
install(FILES "${LDSCRIPT}" DESTINATION lib/ldscripts)
install(FILES mk/mpfs_smode.mk DESTINATION .)
install(FILES mk/toolchain.mk DESTINATION mk)
install(FILES cmake/MpfsSmodeConfig.cmake cmake/MpfsSmodeFlags.cmake cmake/riscv64-unknown-elf.cmake
        DESTINATION lib/cmake/MpfsSmode)

# The settings the libraries were built with, for MpfsSmodeConfig.cmake and
# mpfs_smode.mk (the Makefile writes the same two files).
set(_built_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
set(_mk "# Generated by the mpfs-freertos-smode build: the settings the libraries were built with.\n")
set(_cm "${_mk}")
foreach(_v CROSS_COMPILE BUILD_TYPE MARCH LIBC_SPECS LIBC_LINK_SPECS)
    if(DEFINED _built_${_v})
        set(_val "${_built_${_v}}")
    else()
        set(_val "${${_v}}")
    endif()
    string(APPEND _mk "MPFS_SMODE_BUILT_${_v} := ${_val}\n")
    string(APPEND _cm "set(MPFS_SMODE_BUILT_${_v} \"${_val}\")\n")
endforeach()
set(_cfg "${CMAKE_CURRENT_BINARY_DIR}/install-config")
file(WRITE "${_cfg}/build_config.mk" "${_mk}")
file(WRITE "${_cfg}/build_config.cmake" "${_cm}")
install(FILES "${_cfg}/build_config.mk" DESTINATION mk)
install(FILES "${_cfg}/build_config.cmake" DESTINATION lib/cmake/MpfsSmode)

if(MPFS_SMODE_BUILD_APP)
    add_subdirectory(app)
endif()
