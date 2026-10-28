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
#   MPFS_SMODE_TCP_LIBS libfreertos_tcp (FreeRTOS+TCP + LiteEth interface):
#                       put it before MPFS_SMODE_LIBS if the application
#                       uses networking
#   MPFS_SMODE_DEPS     files to list as link prerequisites
#   CC, OBJCOPY, SIZE, Q  from toolchain.mk
#
# Application-level (the application may change them): MPFS_SMODE_LDSCRIPT
# (default: the installed mpfs-lim-sv39.ld), CROSS_COMPILE, BUILD_TYPE,
# LIBC_LINK_SPECS. Their defaults are the libraries' (mk/build_config.mk).
#
# LIBRARY-FIXED (compiled into the libraries; see MPFS_SMODE_FIXED in
# mk/toolchain.mk): MARCH, LIBC_SPECS, LOADED_BY_BOOTLOADER,
# MPFS_HAL_FIRST_HART, MPFS_HAL_LAST_HART. They are taken from the install
# tree; setting one to a different value stops the build. Rebuild the
# libraries to change them.

ifndef MPFS_SMODE_DIR
  $(error Set MPFS_SMODE_DIR to the installed libraries before including mpfs_smode.mk)
endif

include $(MPFS_SMODE_DIR)/mk/build_config.mk

# Application-level defaults: what the libraries were built with.
CROSS_COMPILE   ?= $(MPFS_SMODE_BUILT_CROSS_COMPILE)
BUILD_TYPE      ?= $(MPFS_SMODE_BUILT_BUILD_TYPE)
LIBC_LINK_SPECS ?= $(MPFS_SMODE_BUILT_LIBC_LINK_SPECS)

# LIBRARY-FIXED: use the libraries' values; refuse different ones.
mpfs_smode_fix = $(if $(filter undefined,$(origin $(1))),\
    $(eval $(1) := $(MPFS_SMODE_BUILT_$(1))),\
    $(if $(subst $(strip $($(1))),,$(strip $(MPFS_SMODE_BUILT_$(1))))$(subst $(strip $(MPFS_SMODE_BUILT_$(1))),,$(strip $($(1)))),\
        $(error $(1) is LIBRARY-FIXED: the libraries in $(MPFS_SMODE_DIR) were built with \
            $(1)=$(MPFS_SMODE_BUILT_$(1)), not $($(1)). Rebuild the libraries to change it)))
$(foreach v,MARCH LIBC_SPECS LOADED_BY_BOOTLOADER MPFS_HAL_FIRST_HART MPFS_HAL_LAST_HART,\
    $(call mpfs_smode_fix,$v))

include $(MPFS_SMODE_DIR)/mk/toolchain.mk

MPFS_SMODE_LDSCRIPT ?= $(MPFS_SMODE_DIR)/lib/ldscripts/mpfs-lim-sv39.ld

MPFS_SMODE_CFLAGS := $(MPFS_CFLAGS) \
                     -I$(MPFS_SMODE_DIR)/include \
                     -I$(MPFS_SMODE_DIR)/include/freertos \
                     -I$(MPFS_SMODE_DIR)/include/freertos_tcp

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

# FreeRTOS+TCP, compiled with the installed include/freertos_tcp/
# FreeRTOSIPConfig.h (LIBRARY-FIXED, like FreeRTOSConfig.h).
MPFS_SMODE_TCP_LIBS := $(MPFS_SMODE_DIR)/lib/libfreertos_tcp.a

# Rewritten only when the libraries' settings change: make objects depend on
# it to rebuild them then (the demo's Makefile does).
MPFS_SMODE_CONFIG := $(MPFS_SMODE_DIR)/mk/build_config.mk

MPFS_SMODE_DEPS := $(MPFS_SMODE_DIR)/lib/libmpfs_smode_boot.a \
                   $(MPFS_SMODE_DIR)/lib/libfreertos_smode.a \
                   $(MPFS_SMODE_DIR)/lib/libmpfs_drivers.a \
                   $(MPFS_SMODE_LDSCRIPT)
