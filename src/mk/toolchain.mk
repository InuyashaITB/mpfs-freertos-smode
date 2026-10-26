# SPDX-License-Identifier: MIT
#
# Tools and compile/link options shared by the library build (src/Makefile)
# and by applications (mpfs_smode.mk). Plain GNU make: it only
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
#   LOADED_BY_BOOTLOADER
#                    0: standalone, this image does the system start-up on the
#                    E51 (clocks, I/O, L2, MPU, PMP) and starts the U54s (default).
#                    1: a previous boot stage (e.g. HSS) did that and starts
#                    this image on the U54(s); the HAL skips the bring-up.
#   MPFS_HAL_FIRST_HART, MPFS_HAL_LAST_HART
#                    harts the HAL starts: 0..4 standalone, 1..1 with a
#                    bootloader (set LAST to 4 if the bootloader starts this
#                    image on all U54s). Must include hart 1.
#   CP               copy command. Default: cp; on Windows 'busybox cp', since
#                    SoftConsole's build_tools has busybox.exe but no cp.exe
#                    (make runs simple recipe lines without a shell there)

# GNU make on Windows runs recipes with sh.exe when it finds one on PATH and
# falls back to cmd.exe otherwise, which cannot run these recipes ('mkdir -p',
# quoting). cmd's echo keeps the quotes, sh's does not.
ifeq ($(OS),Windows_NT)
  ifneq ($(shell echo "sh"),sh)
    $(error No sh.exe on PATH, so make would run recipes with cmd.exe. Add \
      SoftConsole's build_tools\bin to the front of PATH (e.g. in PowerShell: \
      $$env:PATH = "C:\Microchip\SoftConsole-v2022.2-RISC-V-747\build_tools\bin;$$env:PATH") \
      and use the make.exe there)
  endif
endif

CROSS_COMPILE   ?= riscv64-unknown-elf-
LOADED_BY_BOOTLOADER ?= 0
ifeq ($(LOADED_BY_BOOTLOADER),1)
  MPFS_HAL_FIRST_HART ?= 1
  MPFS_HAL_LAST_HART  ?= 1
else ifeq ($(LOADED_BY_BOOTLOADER),0)
  MPFS_HAL_FIRST_HART ?= 0
  MPFS_HAL_LAST_HART  ?= 4
else
  $(error LOADED_BY_BOOTLOADER must be 0 or 1, not '$(LOADED_BY_BOOTLOADER)')
endif
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

# Boot mode for the HAL (mss_sw_config.h). Part of the options for
# applications too, so headers they include see the same configuration.
MPFS_BOOT_DEFS := -DIMAGE_LOADED_BY_BOOTLOADER=$(LOADED_BY_BOOTLOADER) \
                  -DMPFS_HAL_FIRST_HART=$(MPFS_HAL_FIRST_HART) \
                  -DMPFS_HAL_LAST_HART=$(MPFS_HAL_LAST_HART)

# -mcmodel=medany and -fno-jump-tables are what let the virtual-linked image
# run from its physical copy in M-mode (HAL start-up, shim, smode_m_prehook):
# see README.md, "How one image runs both physical and virtual". Code that an
# application runs in M-mode must be built with them too.
MPFS_CFLAGS := -march=$(MARCH) -mabi=lp64d -mcmodel=medany -mstrict-align \
               -fno-jump-tables -ffunction-sections -fdata-sections \
               -fmessage-length=0 -fsigned-char $(LIBC_SPECS) $(MPFS_OPT) \
               $(MPFS_BOOT_DEFS)

MPFS_LDFLAGS := -march=$(MARCH) -mabi=lp64d -mcmodel=medany $(LIBC_SPECS) \
                $(LIBC_LINK_SPECS) -nostartfiles -Wl,--gc-sections

ifeq ($(V),1)
  Q :=
else
  Q := @
endif
