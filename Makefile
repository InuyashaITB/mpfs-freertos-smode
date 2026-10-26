# SPDX-License-Identifier: MIT
#
# Thin wrapper around the CMake build, for the command line and for the
# SoftConsole Makefile project (.cproject calls 'make' / 'make clean').
#
#   make                      configure + build libraries and the demo
#   make install PREFIX=dir   install .a, headers, linker script, CMake package
#   make clean                remove the build directory
#
# Variables (override on the command line):
#   BUILD_DIR               default build
#   BUILD_TYPE              Debug (-O0 -g3) | Release | MinSizeRel
#   PREFIX                  install prefix, default install
#   RISCV_TOOLCHAIN_PREFIX  default riscv64-unknown-elf-
#   RISCV_LIBC_SPECS        default --specs=nano.specs
#   RISCV_LIBC_LINK_SPECS   default --specs=nosys.specs
#   CMAKE_ARGS              anything else for the configure step

CMAKE                  ?= cmake
BUILD_DIR              ?= build
BUILD_TYPE             ?= Debug
PREFIX                 ?= install
RISCV_TOOLCHAIN_PREFIX ?= riscv64-unknown-elf-
RISCV_LIBC_SPECS       ?= --specs=nano.specs
RISCV_LIBC_LINK_SPECS  ?= --specs=nosys.specs
CMAKE_ARGS             ?=

.PHONY: all configure install clean

all: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) \
	    -DCMAKE_TOOLCHAIN_FILE=cmake/riscv64-unknown-elf.cmake \
	    -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	    -DRISCV_TOOLCHAIN_PREFIX="$(RISCV_TOOLCHAIN_PREFIX)" \
	    -DRISCV_LIBC_SPECS="$(RISCV_LIBC_SPECS)" \
	    -DRISCV_LIBC_LINK_SPECS="$(RISCV_LIBC_LINK_SPECS)" \
	    $(CMAKE_ARGS)

install: all
	$(CMAKE) --install $(BUILD_DIR) --prefix $(PREFIX)

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)
