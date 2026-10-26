# SPDX-License-Identifier: MIT
#
# Include this from an application Makefile to build against an installed
# copy of the libraries (the directory 'make install PREFIX=...' created, or
# build/dist in this project):
#
#   MPFS_SMODE_DIR := C:/path/to/mpfs-smode      # or a relative path
#   include $(MPFS_SMODE_DIR)/mpfs_smode.mk
#
#   %.o: %.c
#   	$(CC) $(MPFS_SMODE_CFLAGS) -c $< -o $@
#   app.elf: $(OBJS) $(MPFS_SMODE_DEPS)
#   	$(CC) $(MPFS_SMODE_LDFLAGS) $(OBJS) $(MPFS_SMODE_LIBS) -o $@
#
# Provides:
#   MPFS_SMODE_CFLAGS   compile options + include paths (use for every file;
#                       code run in M-mode needs -mcmodel=medany -fno-jump-tables)
#   MPFS_SMODE_LDFLAGS  link options + linker script
#   MPFS_SMODE_LIBS     the libraries, in the right order (put after objects)
#   MPFS_SMODE_DEPS     files to list as link prerequisites
#   CC, OBJCOPY, SIZE, Q  from toolchain.mk
#
# Overridable: MPFS_SMODE_LDSCRIPT (default: the installed mpfs-lim-sv39.ld),
# and the toolchain.mk variables. Their defaults are the ones the libraries
# were built with (mk/build_config.mk); keep them compatible.

ifndef MPFS_SMODE_DIR
  $(error Set MPFS_SMODE_DIR to the installed libraries before including mpfs_smode.mk)
endif

include $(MPFS_SMODE_DIR)/mk/build_config.mk
include $(MPFS_SMODE_DIR)/mk/toolchain.mk

MPFS_SMODE_LDSCRIPT ?= $(MPFS_SMODE_DIR)/lib/ldscripts/mpfs-lim-sv39.ld

MPFS_SMODE_CFLAGS := $(MPFS_CFLAGS) \
                     -I$(MPFS_SMODE_DIR)/include \
                     -I$(MPFS_SMODE_DIR)/include/freertos

MPFS_SMODE_LDFLAGS := $(MPFS_LDFLAGS) -T$(MPFS_SMODE_LDSCRIPT)

# Whole-archive: weak defaults (the HAL's u54_1(), smode_diag.c's
# smode_os_init()) live in archive members that are always loaded, so
# without it the strong definitions in other members would never be pulled
# in. --gc-sections removes what is unused. The drivers are a normal archive.
MPFS_SMODE_LIBS := -Wl,--whole-archive \
                   $(MPFS_SMODE_DIR)/lib/libmpfs_smode_boot.a \
                   $(MPFS_SMODE_DIR)/lib/libfreertos_smode.a \
                   -Wl,--no-whole-archive \
                   $(MPFS_SMODE_DIR)/lib/libmpfs_drivers.a

MPFS_SMODE_DEPS := $(MPFS_SMODE_DIR)/lib/libmpfs_smode_boot.a \
                   $(MPFS_SMODE_DIR)/lib/libfreertos_smode.a \
                   $(MPFS_SMODE_DIR)/lib/libmpfs_drivers.a \
                   $(MPFS_SMODE_LDSCRIPT)
