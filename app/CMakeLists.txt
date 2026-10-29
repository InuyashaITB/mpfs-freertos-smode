# SPDX-License-Identifier: MIT
#
# The S-mode application: the Sv39 root table, s_mode_main and the counting
# task (main.c). Built by ../CMakeLists.txt against the libraries of that
# tree, or on its own against an installed package:
#
#   cmake -S app -B build-app -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<prefix>/lib/cmake/MpfsSmode/riscv64-unknown-elf.cmake \
#         -DCMAKE_PREFIX_PATH=<prefix>
#   cmake --build build-app              -> build-app/hello.elf, .hex, .map

cmake_minimum_required(VERSION 3.24)
if(NOT TARGET MpfsSmode::mpfs_smode)
    project(mpfs_smode_hello LANGUAGES C)
    find_package(MpfsSmode REQUIRED)
endif()

add_executable(hello main.c)
set_target_properties(hello PROPERTIES SUFFIX ".elf")
target_compile_options(hello PRIVATE -Wall -Wextra)
target_link_libraries(hello PRIVATE MpfsSmode::mpfs_smode)
target_link_options(hello PRIVATE "-Wl,-Map=$<TARGET_FILE_DIR:hello>/hello.map")

add_custom_command(TARGET hello POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:hello> $<TARGET_FILE_DIR:hello>/hello.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:hello>
    VERBATIM)
