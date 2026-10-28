# SPDX-License-Identifier: MIT
#
# Project orchestrator (CMake): the counterpart of the root Makefile. No build
# rules of its own: it adds the library (src/CMakeLists.txt) and the
# application (examples/freertos-demo/CMakeLists.txt), which links the
# library's MpfsSmode:: targets.
#
#   cmake -B build-cmake [-DCMAKE_BUILD_TYPE=Release]
#   cmake --build build-cmake
#   cmake --install build-cmake --prefix <dir>     # the library's install tree
#
# SoftConsole builds with the root Makefile; CMake is an alternative and must
# be installed separately (on Windows also choose -G Ninja, or
# -G "Unix Makefiles" with SoftConsole's build_tools\bin first on PATH).

cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/src/cmake/riscv64-unknown-elf.cmake")
endif()

project(mpfs_freertos_smode LANGUAGES C ASM)

# The demo's FreeRTOSConfig.h is compiled into the library, as the root
# Makefile does (relative to src/).
set(FREERTOS_CONFIG_DIR "../examples/freertos-demo" CACHE PATH
    "Directory with the FreeRTOSConfig.h compiled into libfreertos_smode")
set(FREERTOS_IP_CONFIG_DIR "../examples/freertos-demo" CACHE PATH
    "Directory with the FreeRTOSIPConfig.h compiled into libfreertos_tcp")

add_subdirectory(src)
add_subdirectory(examples/freertos-demo)
