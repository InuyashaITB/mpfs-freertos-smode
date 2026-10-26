# SPDX-License-Identifier: MIT
#
# Tools and compile/link options shared by the library build (top-level
# Makefile) and by applications (mpfs_smode.mk). Plain GNU make: it only
# needs what SoftConsole ships in build_tools (make, sh, mkdir, cp, rm) and
# the riscv64-unknown-elf toolchain on PATH.
#
# Variables (set on the command line or before including this file):
#   CROSS_COMPILE    tool prefix, may include a directory   riscv64-unknown-elf-
#   BUILD_TYPE       Debug (-O0 -g3) or Release (-Os -g)    Debug
#   LIBC_SPECS       C library specs, compile + link        --specs=nano.specs
#   LIBC_LINK_SPECS  extra link-only specs                  --specs=nosys.specs
#                    (picolibc toolchains: LIBC_SPECS=--specs=picolibc.specs
#                    LIBC_LINK_SPECS= )
#   MARCH            -march value; default rv64gc, plus _zicsr_zifencei on
#                    GCC >= 12 (which split them out of 'g')
#   V=1              print full command lines
#   CP               copy command. Default: cp; on Windows 'busybox cp', since
#                    SoftConsole's build_tools has busybox.exe but no cp.exe
#                    (make runs simple recipe lines without a shell there)

CROSS_COMPILE   ?= riscv64-unknown-elf-
BUILD_TYPE      ?= Debug
LIBC_SPECS      ?= --specs=nano.specs
LIBC_LINK_SPECS ?= --specs=nosys.specs

ifeq ($(OS),Windows_NT)
  CP ?= busybox cp
else
  CP ?= cp
endif

CC      := $(CROSS_COMPILE)gcc
AR      := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

# (Not needed, and the compiler need not be on PATH, for 'make clean'.)
ifeq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
  MARCH ?= rv64gc
endif

ifndef MARCH
  MPFS_GCC_MAJOR := $(firstword $(subst ., ,$(shell $(CC) -dumpversion)))
  ifeq ($(MPFS_GCC_MAJOR),)
    $(error $(CC) not found: put the RISC-V toolchain on PATH or set CROSS_COMPILE)
  endif
  ifneq ($(filter 5 6 7 8 9 10 11,$(MPFS_GCC_MAJOR)),)
    MARCH := rv64gc
  else
    MARCH := rv64gc_zicsr_zifencei
  endif
endif

ifeq ($(BUILD_TYPE),Debug)
  MPFS_OPT := -O0 -g3
else ifeq ($(BUILD_TYPE),Release)
  MPFS_OPT := -Os -g -DNDEBUG
else
  $(error BUILD_TYPE must be Debug or Release, not '$(BUILD_TYPE)')
endif

# -mcmodel=medany and -fno-jump-tables are what let the virtual-linked image
# run from its physical copy in M-mode (HAL start-up, shim, smode_m_prehook):
# see README.md, "How one image runs both physical and virtual". Code that an
# application runs in M-mode must be built with them too.
MPFS_CFLAGS := -march=$(MARCH) -mabi=lp64d -mcmodel=medany -mstrict-align \
               -fno-jump-tables -ffunction-sections -fdata-sections \
               -fmessage-length=0 -fsigned-char $(LIBC_SPECS) $(MPFS_OPT)

MPFS_LDFLAGS := -march=$(MARCH) -mabi=lp64d -mcmodel=medany $(LIBC_SPECS) \
                $(LIBC_LINK_SPECS) -nostartfiles -Wl,--gc-sections

ifeq ($(V),1)
  Q :=
else
  Q := @
endif
