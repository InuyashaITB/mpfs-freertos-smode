# SPDX-License-Identifier: MIT
#
# Toolchain and flags shared by the library build (../Makefile) and
# applications (installed with the libraries, included by mpfs_smode.mk).
# cmake/MpfsSmodeFlags.cmake has the same flags for CMake.
#
#   CROSS_COMPILE    default riscv64-unknown-elf-
#   BUILD_TYPE       Debug (-O0 -g3, default) or Release (-Os -g)
#   MARCH            default rv64gc, rv64gc_zicsr_zifencei on GCC >= 12
#   LIBC_SPECS       default --specs=nano.specs (picolibc: --specs=picolibc.specs)
#   LIBC_LINK_SPECS  default --specs=nosys.specs (picolibc: empty)

CROSS_COMPILE   ?= riscv64-unknown-elf-
BUILD_TYPE      ?= Debug
LIBC_SPECS      ?= --specs=nano.specs
LIBC_LINK_SPECS ?= --specs=nosys.specs

CC      := $(CROSS_COMPILE)gcc
AR      := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

ifndef MARCH
  GCC_MAJOR := $(firstword $(subst ., ,$(shell $(CC) -dumpversion)))
  MARCH := $(if $(filter 1 2 3 4 5 6 7 8 9 10 11,$(GCC_MAJOR)),rv64gc,rv64gc_zicsr_zifencei)
endif

MPFS_OPT := $(if $(filter Release,$(BUILD_TYPE)),-Os -g,-O0 -g3)

MPFS_CFLAGS := -march=$(MARCH) -mabi=lp64d -mcmodel=medany -mstrict-align -fno-jump-tables \
               -ffunction-sections -fdata-sections -fmessage-length=0 $(LIBC_SPECS) $(MPFS_OPT)
MPFS_LDFLAGS := -march=$(MARCH) -mabi=lp64d -mcmodel=medany $(LIBC_SPECS) $(LIBC_LINK_SPECS) \
                -nostartfiles -Wl,--gc-sections
