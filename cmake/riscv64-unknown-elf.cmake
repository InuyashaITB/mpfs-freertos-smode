# SPDX-License-Identifier: MIT
#
# CMake toolchain file for bare-metal RISC-V (PolarFire SoC U54 / E51).
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv64-unknown-elf.cmake
#
# RISCV_TOOLCHAIN_PREFIX  tool prefix, may include a directory. Default:
#                         riscv64-unknown-elf- (SoftConsole's toolchain, on PATH)
#                         e.g. /opt/xpack-riscv-none-elf-gcc/bin/riscv-none-elf-
# RISCV_LIBC_SPECS        GCC specs selecting the C library (compile + link).
#                         Default: --specs=nano.specs (newlib-nano, as in
#                         SoftConsole). picolibc toolchains: --specs=picolibc.specs
# RISCV_LIBC_LINK_SPECS   extra link-only specs. Default: --specs=nosys.specs
#                         (newlib). picolibc toolchains: empty.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(RISCV_TOOLCHAIN_PREFIX "riscv64-unknown-elf-" CACHE STRING "RISC-V GCC tool prefix")
set(RISCV_LIBC_SPECS "--specs=nano.specs" CACHE STRING "GCC specs selecting the C library")
set(RISCV_LIBC_LINK_SPECS "--specs=nosys.specs" CACHE STRING "Extra link-only GCC specs")

set(CMAKE_C_COMPILER   "${RISCV_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${RISCV_TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${RISCV_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_AR           "${RISCV_TOOLCHAIN_PREFIX}ar" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${RISCV_TOOLCHAIN_PREFIX}ranlib" CACHE FILEPATH "")
set(CMAKE_OBJCOPY      "${RISCV_TOOLCHAIN_PREFIX}objcopy" CACHE FILEPATH "")
set(CMAKE_OBJDUMP      "${RISCV_TOOLCHAIN_PREFIX}objdump" CACHE FILEPATH "")
set(CMAKE_SIZE         "${RISCV_TOOLCHAIN_PREFIX}size" CACHE FILEPATH "")

# Compiler checks cannot link a bare-metal executable without a linker script.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Pass the cache variables on to try_compile() projects.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RISCV_TOOLCHAIN_PREFIX RISCV_LIBC_SPECS RISCV_LIBC_LINK_SPECS)

# $<LINK_LIBRARY:WHOLE_ARCHIVE,...> is only predefined for hosted platforms.
set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE
    "LINKER:--whole-archive" "<LINK_ITEM>" "LINKER:--no-whole-archive")
set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED TRUE)
