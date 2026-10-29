# SPDX-License-Identifier: MIT
#
# CMake toolchain file for bare-metal RISC-V (PolarFire SoC).
#
#   cmake -B build-cmake -DCMAKE_TOOLCHAIN_FILE=cmake/riscv64-unknown-elf.cmake
#
# CROSS_COMPILE   tool prefix, may include a directory (default riscv64-unknown-elf-),
#                 same as the make build
#
# On Windows, also pick a generator: -G Ninja, or -G "Unix Makefiles" with
# SoftConsole's build_tools\bin (make + sh) first on PATH.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(CROSS_COMPILE "riscv64-unknown-elf-" CACHE STRING "RISC-V GCC tool prefix")

# Every tool as a full path next to gcc: on Windows, Ninja runs ar and ranlib
# through cmd.exe, whose PATH need not have the toolchain.
find_program(RISCV_GCC "${CROSS_COMPILE}gcc" REQUIRED)
get_filename_component(_rv_bin "${RISCV_GCC}" DIRECTORY)
get_filename_component(_rv_prefix "${CROSS_COMPILE}" NAME)
set(_rv "${_rv_bin}/${_rv_prefix}")
if(CMAKE_HOST_WIN32)
    set(_rv_exe ".exe")
endif()

set(CMAKE_C_COMPILER   "${_rv}gcc${_rv_exe}")
set(CMAKE_ASM_COMPILER "${_rv}gcc${_rv_exe}")
set(CMAKE_AR           "${_rv}ar${_rv_exe}"      CACHE FILEPATH "")
set(CMAKE_RANLIB       "${_rv}ranlib${_rv_exe}"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY      "${_rv}objcopy${_rv_exe}" CACHE FILEPATH "")
set(CMAKE_SIZE         "${_rv}size${_rv_exe}"    CACHE FILEPATH "")

# Compiler checks cannot link a bare-metal executable without a linker script.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CROSS_COMPILE)

# Same optimisation as the make build (BUILD_TYPE). Set as cache entries
# before project() so CMake's own defaults (-O3 for Release) never apply.
set(CMAKE_C_FLAGS_DEBUG     "-O0 -g3"         CACHE STRING "")
set(CMAKE_ASM_FLAGS_DEBUG   "-O0 -g3"         CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE   "-Os -g -DNDEBUG" CACHE STRING "")
set(CMAKE_ASM_FLAGS_RELEASE "-Os -g -DNDEBUG" CACHE STRING "")
