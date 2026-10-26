# SPDX-License-Identifier: MIT
#
# PolarFire SoC: FreeRTOS in S-mode on U54_1, as static libraries.
#
#   libmpfs_smode_boot.a  MPFS HAL start-up + U54_1 M-mode set-up, M-mode SBI
#                         shim, Sv39 bring-up, S-mode entry -> s_mode_main()
#   libfreertos_smode.a   FreeRTOS kernel + S-mode port + trap glue, built
#                         against FREERTOS_CONFIG_DIR/FreeRTOSConfig.h
#   libmpfs_drivers.a     MSS drivers listed in MPFS_DRIVERS (for applications)
#
# Build, install and (by default) the demo application:
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv64-unknown-elf.cmake
#   cmake --build build
#   cmake --install build --prefix <dir>
#
# An application links MpfsSmode::mpfs_smode (both libraries, whole-archive,
# plus the linker script) and optionally MpfsSmode::mpfs_drivers; see
# examples/freertos-demo/CMakeLists.txt.

cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/riscv64-unknown-elf.cmake")
endif()

project(mpfs_smode VERSION 1.0.0 LANGUAGES C ASM)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug, Release or MinSizeRel" FORCE)
endif()

option(MPFS_SMODE_BUILD_EXAMPLE "Build examples/freertos-demo" ON)

set(MPFS_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/boards/icicle-kit-es" CACHE PATH
    "Board directory (fpga_design_config/, platform_config/mpfs_hal_config/)")
set(MPFS_LINKER_SCRIPT "${MPFS_BOARD_DIR}/platform_config/linker/mpfs-lim-sv39.ld" CACHE FILEPATH
    "Linker script (Sv39 layout) installed with the libraries")
set(FREERTOS_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/examples/freertos-demo" CACHE PATH
    "Directory holding the FreeRTOSConfig.h libfreertos_smode is built against")
set(MPFS_DRIVERS "mss_mmuart;mss_gpio" CACHE STRING
    "MSS drivers (directories under src/platform/drivers/mss) built into libmpfs_drivers")

if(NOT EXISTS "${FREERTOS_CONFIG_DIR}/FreeRTOSConfig.h")
    message(FATAL_ERROR "FREERTOS_CONFIG_DIR (${FREERTOS_CONFIG_DIR}) has no FreeRTOSConfig.h")
endif()

set(SRC      "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PLATFORM "${SRC}/platform")
set(KERNEL   "${SRC}/middleware/FreeRTOS-Kernel")
set(PORT     "${KERNEL}/portable/GCC/RISC-V-SMode")

#-------------------------------------------------------------------------------
# Compile / link options shared by the libraries and every application.
#
# -mcmodel=medany and -fno-jump-tables are what let the virtual-linked image
# run from its physical copy in M-mode (HAL start-up, shim, smode_m_prehook);
# see README.md, "How one image runs both physical and virtual".
#-------------------------------------------------------------------------------
include(CheckCCompilerFlag)
# GCC >= 12 / binutils >= 2.38 split Zicsr and Zifencei out of 'g'.
check_c_compiler_flag("-march=rv64gc_zicsr_zifencei -mabi=lp64d" MPFS_HAVE_ZICSR)
if(MPFS_HAVE_ZICSR)
    set(MPFS_MARCH "-march=rv64gc_zicsr_zifencei")
else()
    set(MPFS_MARCH "-march=rv64gc")
endif()

separate_arguments(_libc_specs NATIVE_COMMAND "${RISCV_LIBC_SPECS}")
separate_arguments(_libc_link_specs NATIVE_COMMAND "${RISCV_LIBC_LINK_SPECS}")

add_library(mpfs_smode_options INTERFACE)
target_compile_options(mpfs_smode_options INTERFACE
    ${MPFS_MARCH} -mabi=lp64d -mcmodel=medany -mstrict-align
    -fno-jump-tables -ffunction-sections -fdata-sections -fmessage-length=0
    -fsigned-char ${_libc_specs}
    $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-Os>)   # after CMake's -O3: size, as SoftConsole's release builds
target_link_options(mpfs_smode_options INTERFACE
    ${MPFS_MARCH} -mabi=lp64d -mcmodel=medany ${_libc_specs} ${_libc_link_specs}
    -nostartfiles -Wl,--gc-sections
    $<BUILD_INTERFACE:-T${MPFS_LINKER_SCRIPT}>)
# The install side adds -T<prefix>/lib/ldscripts/... in MpfsSmodeConfig.cmake.

# Header search paths, as seen by code: "mpfs_hal/...", "drivers/mss/...",
# "fpga_design_config/...", "mpfs_hal_config/...", "smode/...".
add_library(mpfs_smode_headers INTERFACE)
target_include_directories(mpfs_smode_headers INTERFACE
    $<BUILD_INTERFACE:${PLATFORM}>
    $<BUILD_INTERFACE:${MPFS_BOARD_DIR}>
    $<BUILD_INTERFACE:${MPFS_BOARD_DIR}/platform_config>
    $<BUILD_INTERFACE:${SRC}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(mpfs_smode_headers INTERFACE mpfs_smode_options)

add_library(freertos_smode_headers INTERFACE)
target_include_directories(freertos_smode_headers INTERFACE
    $<BUILD_INTERFACE:${FREERTOS_CONFIG_DIR}>
    $<BUILD_INTERFACE:${KERNEL}/include>
    $<BUILD_INTERFACE:${PORT}>
    $<INSTALL_INTERFACE:include/freertos>)
target_link_libraries(freertos_smode_headers INTERFACE mpfs_smode_headers)

#-------------------------------------------------------------------------------
# libmpfs_smode_boot.a
#-------------------------------------------------------------------------------
file(GLOB_RECURSE _hal_sources CONFIGURE_DEPENDS
    "${PLATFORM}/mpfs_hal/*.c" "${PLATFORM}/mpfs_hal/*.S"
    "${PLATFORM}/hal/*.c" "${PLATFORM}/hal/*.S")

set(_smode_sources
    "${SRC}/smode/u54_1_boot.c"
    "${SRC}/smode/m_shim.c"
    "${SRC}/smode/m_shim_trap.S"
    "${SRC}/smode/smode_start.S"
    "${SRC}/smode/smode_diag.c"
    "${SRC}/smode/sv39.c")

add_library(mpfs_smode_boot STATIC ${_hal_sources} ${_smode_sources})
target_link_libraries(mpfs_smode_boot PUBLIC mpfs_smode_headers)
set_source_files_properties(${_smode_sources} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

#-------------------------------------------------------------------------------
# libfreertos_smode.a
#-------------------------------------------------------------------------------
set(_glue_sources "${SRC}/freertos_glue/smode_irq.c")

add_library(freertos_smode STATIC
    "${KERNEL}/croutine.c"
    "${KERNEL}/event_groups.c"
    "${KERNEL}/list.c"
    "${KERNEL}/queue.c"
    "${KERNEL}/stream_buffer.c"
    "${KERNEL}/tasks.c"
    "${KERNEL}/timers.c"
    "${KERNEL}/portable/MemMang/heap_4.c"
    "${PORT}/port.c"
    "${PORT}/portASM.S"
    ${_glue_sources})
target_link_libraries(freertos_smode PUBLIC freertos_smode_headers)
set_source_files_properties(${_glue_sources} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

#-------------------------------------------------------------------------------
# libmpfs_drivers.a
#-------------------------------------------------------------------------------
set(_driver_sources)
foreach(_drv IN LISTS MPFS_DRIVERS)
    if(NOT IS_DIRECTORY "${PLATFORM}/drivers/mss/${_drv}")
        message(FATAL_ERROR "MPFS_DRIVERS: no driver directory src/platform/drivers/mss/${_drv}")
    endif()
    file(GLOB_RECURSE _drv_src CONFIGURE_DEPENDS
        "${PLATFORM}/drivers/mss/${_drv}/*.c" "${PLATFORM}/drivers/mss/${_drv}/*.S")
    list(APPEND _driver_sources ${_drv_src})
endforeach()

add_library(mpfs_drivers STATIC ${_driver_sources})
target_link_libraries(mpfs_drivers PUBLIC mpfs_smode_headers)

#-------------------------------------------------------------------------------
# What an application links. Whole-archive is required: weak defaults (the
# HAL's u54_1(), smode_diag.c's smode_os_init()) live in members that are
# always pulled in, so the strong definitions in other members would
# otherwise never be loaded. --gc-sections drops whatever is unused.
#-------------------------------------------------------------------------------
add_library(mpfs_smode INTERFACE)
# (install(EXPORT) does not namespace target names inside $<LINK_LIBRARY>.)
target_link_libraries(mpfs_smode INTERFACE
    "$<BUILD_INTERFACE:$<LINK_LIBRARY:WHOLE_ARCHIVE,mpfs_smode_boot,freertos_smode>>"
    "$<INSTALL_INTERFACE:$<LINK_LIBRARY:WHOLE_ARCHIVE,MpfsSmode::mpfs_smode_boot,MpfsSmode::freertos_smode>>"
    freertos_smode_headers)

foreach(_t mpfs_smode_options mpfs_smode_headers freertos_smode_headers
           mpfs_smode_boot freertos_smode mpfs_drivers mpfs_smode)
    add_library(MpfsSmode::${_t} ALIAS ${_t})
endforeach()

#-------------------------------------------------------------------------------
# Install: <prefix>/lib/*.a, lib/ldscripts/, lib/cmake/MpfsSmode/, include/
#-------------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS mpfs_smode_options mpfs_smode_headers freertos_smode_headers
                mpfs_smode_boot freertos_smode mpfs_drivers mpfs_smode
        EXPORT MpfsSmodeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY "${PLATFORM}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
        PATTERN "platform_config_reference" EXCLUDE
        PATTERN "soc_config_generator" EXCLUDE)
install(DIRECTORY "${MPFS_BOARD_DIR}/fpga_design_config"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${MPFS_BOARD_DIR}/platform_config/mpfs_hal_config"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${SRC}/smode" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${KERNEL}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/freertos
        FILES_MATCHING PATTERN "*.h")
install(FILES "${PORT}/portmacro.h" "${PORT}/portContext.h"
              "${PORT}/freertos_risc_v_chip_specific_extensions.h"
              "${FREERTOS_CONFIG_DIR}/FreeRTOSConfig.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/freertos)
install(FILES "${MPFS_LINKER_SCRIPT}" DESTINATION ${CMAKE_INSTALL_LIBDIR}/ldscripts)

get_filename_component(MPFS_LINKER_SCRIPT_NAME "${MPFS_LINKER_SCRIPT}" NAME)
set(MPFS_SMODE_LIBDIR ${CMAKE_INSTALL_LIBDIR})
configure_package_config_file(cmake/MpfsSmodeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/MpfsSmodeConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MpfsSmode
    PATH_VARS MPFS_SMODE_LIBDIR)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MpfsSmodeConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
install(EXPORT MpfsSmodeTargets NAMESPACE MpfsSmode::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MpfsSmode)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MpfsSmodeConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/MpfsSmodeConfigVersion.cmake"
              "${CMAKE_CURRENT_LIST_DIR}/cmake/riscv64-unknown-elf.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MpfsSmode)

#-------------------------------------------------------------------------------
# Demo application
#-------------------------------------------------------------------------------
if(MPFS_SMODE_BUILD_EXAMPLE)
    add_subdirectory(examples/freertos-demo)
endif()
