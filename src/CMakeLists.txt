# SPDX-License-Identifier: MIT
#
# Library build (CMake): PolarFire SoC FreeRTOS in S-mode on U54_1, as static
# libraries plus an install tree. Knows nothing about any application. The
# CMake counterpart of src/Makefile: same sources, options and install tree.
#
#   libmpfs_smode_boot.a  MPFS HAL start-up + U54_1 M-mode set-up, M-mode SBI
#                         shim, Sv39 bring-up, S-mode entry -> s_mode_main()
#   libfreertos_smode.a   FreeRTOS kernel + S-mode port + trap glue, built
#                         against ${FREERTOS_CONFIG_DIR}/FreeRTOSConfig.h
#   libmpfs_drivers.a     MSS drivers listed in MPFS_DRIVERS (for applications)
#
# Standalone:
#   cmake -S src -B build-lib [-DCMAKE_BUILD_TYPE=Release]
#   cmake --build build-lib
#   cmake --install build-lib --prefix <dir>
#
# In-tree (root CMakeLists.txt) it also defines the MpfsSmode:: targets an
# application links, so no install step is needed there.

cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/riscv64-unknown-elf.cmake")
endif()

project(mpfs_smode VERSION 1.0.0 LANGUAGES C ASM)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug (-O0 -g3) or Release (-Os -g)" FORCE)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/MpfsSmodeFlags.cmake")
mpfs_smode_default_march(_default_march)

# Same variables as src/Makefile; relative paths are relative to src/.
set(FREERTOS_CONFIG_DIR "freertos_glue/config" CACHE PATH
    "Directory with the FreeRTOSConfig.h compiled into libfreertos_smode (installed with it)")
set(MPFS_BOARD_DIR "boards/icicle-kit-es" CACHE PATH "Board configuration headers")
set(MPFS_LINKER_SCRIPT "" CACHE FILEPATH
    "Linker script installed for applications (default: <board>/platform_config/linker/mpfs-lim-sv39.ld)")
set(MPFS_DRIVERS "mss_mmuart;mss_gpio" CACHE STRING "drivers/mss/<name> built into libmpfs_drivers")
set(LIBC_SPECS "--specs=nano.specs" CACHE STRING
    "C library specs, compile + link (picolibc: --specs=picolibc.specs)")
set(LIBC_LINK_SPECS "--specs=nosys.specs" CACHE STRING "Extra link-only specs (picolibc: empty)")
set(MARCH "${_default_march}" CACHE STRING "-march value")
option(LOADED_BY_BOOTLOADER
    "ON: a previous boot stage (e.g. HSS) did the system start-up and starts this image on the U54(s); OFF: standalone"
    OFF)
if(LOADED_BY_BOOTLOADER)
    set(_boot 1)
    set(_first_default 1)
    set(_last_default 1)
else()
    set(_boot 0)
    set(_first_default 0)
    set(_last_default 4)
endif()
# Empty = follow LOADED_BY_BOOTLOADER (0..4 standalone, 1..1 with a bootloader).
set(MPFS_HAL_FIRST_HART "" CACHE STRING "First hart the HAL starts (0..4); empty = automatic")
set(MPFS_HAL_LAST_HART "" CACHE STRING "Last hart the HAL starts (0..4), must include hart 1; empty = automatic")
if(MPFS_HAL_FIRST_HART STREQUAL "")
    set(_first_hart ${_first_default})
else()
    set(_first_hart ${MPFS_HAL_FIRST_HART})
endif()
if(MPFS_HAL_LAST_HART STREQUAL "")
    set(_last_hart ${_last_default})
else()
    set(_last_hart ${MPFS_HAL_LAST_HART})
endif()

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(_config_dir "${FREERTOS_CONFIG_DIR}" ABSOLUTE BASE_DIR "${SRC}")
get_filename_component(_board_dir  "${MPFS_BOARD_DIR}" ABSOLUTE BASE_DIR "${SRC}")
if(MPFS_LINKER_SCRIPT)
    get_filename_component(_ldscript "${MPFS_LINKER_SCRIPT}" ABSOLUTE BASE_DIR "${SRC}")
else()
    set(_ldscript "${_board_dir}/platform_config/linker/mpfs-lim-sv39.ld")
endif()
if(NOT EXISTS "${_config_dir}/FreeRTOSConfig.h")
    message(FATAL_ERROR "FREERTOS_CONFIG_DIR (${_config_dir}) has no FreeRTOSConfig.h")
endif()

set(PLATFORM "${SRC}/platform")
set(KERNEL   "${SRC}/middleware/FreeRTOS-Kernel")
set(PORT     "${KERNEL}/portable/GCC/RISC-V-SMode")

mpfs_smode_boot_defs(${_boot} ${_first_hart} ${_last_hart} MPFS_BOOT_DEFS)
mpfs_smode_flags("${MARCH}" "${LIBC_SPECS}" "${LIBC_LINK_SPECS}" "${MPFS_BOOT_DEFS}"
                 MPFS_CFLAGS MPFS_LDFLAGS)

#-------------------------------------------------------------------------------
# Sources. The explicit lists must match src/Makefile.
#-------------------------------------------------------------------------------
file(GLOB_RECURSE HAL_SRCS CONFIGURE_DEPENDS
    "${PLATFORM}/mpfs_hal/*.c" "${PLATFORM}/mpfs_hal/*.S"
    "${PLATFORM}/hal/*.c" "${PLATFORM}/hal/*.S")

set(SMODE_SRCS
    "${SRC}/smode/u54_1_boot.c"
    "${SRC}/smode/m_shim.c"
    "${SRC}/smode/m_shim_trap.S"
    "${SRC}/smode/smode_start.S"
    "${SRC}/smode/smode_diag.c"
    "${SRC}/smode/sv39.c")

set(GLUE_SRCS "${SRC}/freertos_glue/smode_irq.c")

set(KERNEL_SRCS
    "${KERNEL}/croutine.c"
    "${KERNEL}/event_groups.c"
    "${KERNEL}/list.c"
    "${KERNEL}/queue.c"
    "${KERNEL}/stream_buffer.c"
    "${KERNEL}/tasks.c"
    "${KERNEL}/timers.c"
    "${KERNEL}/portable/MemMang/heap_4.c"
    "${PORT}/port.c"
    "${PORT}/portASM.S")

set(DRIVER_SRCS)
foreach(_drv IN LISTS MPFS_DRIVERS)
    if(NOT IS_DIRECTORY "${PLATFORM}/drivers/mss/${_drv}")
        message(FATAL_ERROR "MPFS_DRIVERS: no driver directory ${PLATFORM}/drivers/mss/${_drv}")
    endif()
    file(GLOB_RECURSE _drv_srcs CONFIGURE_DEPENDS
        "${PLATFORM}/drivers/mss/${_drv}/*.c" "${PLATFORM}/drivers/mss/${_drv}/*.S")
    list(APPEND DRIVER_SRCS ${_drv_srcs})
endforeach()

# Our own sources get the extra warnings; vendor code builds as before.
set_source_files_properties(${SMODE_SRCS} ${GLUE_SRCS} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# Header search paths, as seen by code: "mpfs_hal/...", "drivers/mss/...",
# "fpga_design_config/...", "mpfs_hal_config/...", "smode/...", "FreeRTOS.h".
set(LIB_INCLUDES
    "${PLATFORM}"
    "${_board_dir}"
    "${_board_dir}/platform_config"
    "${SRC}"
    "${_config_dir}"
    "${KERNEL}/include"
    "${PORT}")

#-------------------------------------------------------------------------------
# Libraries
#-------------------------------------------------------------------------------
add_library(mpfs_smode_boot STATIC ${HAL_SRCS} ${SMODE_SRCS})
add_library(freertos_smode STATIC ${KERNEL_SRCS} ${GLUE_SRCS})
add_library(mpfs_drivers STATIC ${DRIVER_SRCS})

foreach(_lib mpfs_smode_boot freertos_smode mpfs_drivers)
    target_compile_options(${_lib} PRIVATE ${MPFS_CFLAGS})
    target_include_directories(${_lib} PRIVATE ${LIB_INCLUDES})
endforeach()

#-------------------------------------------------------------------------------
# In-tree consumer targets: the same names and properties MpfsSmodeConfig.cmake
# gives an installed copy, but on the source tree.
#-------------------------------------------------------------------------------
add_library(mpfs_smode_headers INTERFACE)
target_include_directories(mpfs_smode_headers INTERFACE ${LIB_INCLUDES})
target_compile_options(mpfs_smode_headers INTERFACE ${MPFS_CFLAGS})
target_link_libraries(mpfs_drivers INTERFACE mpfs_smode_headers)

# Whole-archive: see MpfsSmodeConfig.cmake.
add_library(mpfs_smode INTERFACE)
target_link_options(mpfs_smode INTERFACE ${MPFS_LDFLAGS} "-T${_ldscript}")
target_link_libraries(mpfs_smode INTERFACE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,mpfs_smode_boot,freertos_smode>"
    mpfs_smode_headers)
set_property(TARGET mpfs_smode APPEND PROPERTY INTERFACE_LINK_DEPENDS "${_ldscript}")

foreach(_t mpfs_smode_boot freertos_smode mpfs_drivers mpfs_smode_headers mpfs_smode)
    add_library(MpfsSmode::${_t} ALIAS ${_t})
endforeach()

#-------------------------------------------------------------------------------
# Install tree: the same layout as src/Makefile's build/dist
#
#   lib/*.a, lib/ldscripts/mpfs-lim-sv39.ld
#   include/{mpfs_hal,hal,drivers,fpga_design_config,mpfs_hal_config,smode}/
#   include/freertos/       kernel + port headers, the FreeRTOSConfig.h used
#   mpfs_smode.mk, mk/toolchain.mk, mk/build_config.mk          (make apps)
#   lib/cmake/MpfsSmode/                                        (CMake apps)
#-------------------------------------------------------------------------------
install(TARGETS mpfs_smode_boot freertos_smode mpfs_drivers ARCHIVE DESTINATION lib)

install(DIRECTORY "${PLATFORM}/" DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${_board_dir}/fpga_design_config" DESTINATION include
        FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${_board_dir}/platform_config/mpfs_hal_config" DESTINATION include
        FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${SRC}/smode" DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${KERNEL}/include/" DESTINATION include/freertos
        FILES_MATCHING PATTERN "*.h")
install(FILES "${PORT}/portmacro.h" "${PORT}/portContext.h"
              "${PORT}/freertos_risc_v_chip_specific_extensions.h"
              "${_config_dir}/FreeRTOSConfig.h"
        DESTINATION include/freertos)
install(FILES "${_ldscript}" DESTINATION lib/ldscripts RENAME mpfs-lim-sv39.ld)

install(FILES "${SRC}/mk/mpfs_smode.mk" DESTINATION .)
install(FILES "${SRC}/mk/toolchain.mk" DESTINATION mk)
install(FILES "${SRC}/cmake/MpfsSmodeConfig.cmake"
              "${SRC}/cmake/MpfsSmodeFlags.cmake"
              "${SRC}/cmake/riscv64-unknown-elf.cmake"
        DESTINATION lib/cmake/MpfsSmode)

# The toolchain settings the libraries were built with, for both kinds of
# application (same content as src/Makefile writes).
set(_cfg "${CMAKE_CURRENT_BINARY_DIR}/install-config")
file(WRITE "${_cfg}/build_config.mk"
    "# Generated by the mpfs-freertos-smode build: settings the libraries were built with.\n"
    "CROSS_COMPILE ?= ${CROSS_COMPILE}\n"
    "BUILD_TYPE ?= ${CMAKE_BUILD_TYPE}\n"
    "LIBC_SPECS ?= ${LIBC_SPECS}\n"
    "LIBC_LINK_SPECS ?= ${LIBC_LINK_SPECS}\n"
    "MARCH ?= ${MARCH}\n"
    "LOADED_BY_BOOTLOADER ?= ${_boot}\n"
    "MPFS_HAL_FIRST_HART ?= ${_first_hart}\n"
    "MPFS_HAL_LAST_HART ?= ${_last_hart}\n")
file(WRITE "${_cfg}/build_config.cmake"
    "# Generated by the mpfs-freertos-smode build: settings the libraries were built with.\n"
    "set(MPFS_SMODE_CROSS_COMPILE \"${CROSS_COMPILE}\")\n"
    "set(MPFS_SMODE_BUILD_TYPE \"${CMAKE_BUILD_TYPE}\")\n"
    "set(MPFS_SMODE_LIBC_SPECS \"${LIBC_SPECS}\")\n"
    "set(MPFS_SMODE_LIBC_LINK_SPECS \"${LIBC_LINK_SPECS}\")\n"
    "set(MPFS_SMODE_MARCH \"${MARCH}\")\n"
    "set(MPFS_SMODE_BOOT_DEFS \"${MPFS_BOOT_DEFS}\")\n")
install(FILES "${_cfg}/build_config.mk" DESTINATION mk)
install(FILES "${_cfg}/build_config.cmake" DESTINATION lib/cmake/MpfsSmode)
