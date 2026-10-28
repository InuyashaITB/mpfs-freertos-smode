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

set(CMAKE_C_COMPILER   "${CROSS_COMPILE}gcc")
set(CMAKE_ASM_COMPILER "${CROSS_COMPILE}gcc")
set(CMAKE_AR           "${CROSS_COMPILE}ar"      CACHE FILEPATH "")
set(CMAKE_RANLIB       "${CROSS_COMPILE}ranlib"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY      "${CROSS_COMPILE}objcopy" CACHE FILEPATH "")
set(CMAKE_SIZE         "${CROSS_COMPILE}size"    CACHE FILEPATH "")

# Compiler checks cannot link a bare-metal executable without a linker script.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CROSS_COMPILE)

# Same optimisation as the make build (BUILD_TYPE). Set as cache entries
# before project() so CMake's own defaults (-O3 for Release) never apply.
set(CMAKE_C_FLAGS_DEBUG     "-O0 -g3"         CACHE STRING "")
set(CMAKE_ASM_FLAGS_DEBUG   "-O0 -g3"         CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE   "-Os -g -DNDEBUG" CACHE STRING "")
set(CMAKE_ASM_FLAGS_RELEASE "-Os -g -DNDEBUG" CACHE STRING "")
