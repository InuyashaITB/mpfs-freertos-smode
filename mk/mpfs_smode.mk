# SPDX-License-Identifier: MIT
#
# For application Makefiles: set MPFS_SMODE_DIR to the installed libraries
# (make install / cmake --install prefix, or the staged build/dist), then
#
#   include $(MPFS_SMODE_DIR)/mpfs_smode.mk
#
# Provides CC, OBJCOPY, SIZE and
#   MPFS_SMODE_CFLAGS   compile flags, include paths (mpfs.h, FreeRTOS and the
#                       FreeRTOSConfig.h the libraries were built with)
#   MPFS_SMODE_LDFLAGS  link flags and the linker script
#   MPFS_SMODE_LIBS     both libraries, whole-archive: nothing in an
#                       application refers to the boot code (the bootloader
#                       jumps to _start), so a plain archive link would drop it
#   MPFS_SMODE_DEPS     the files a link depends on
#   MPFS_SMODE_CONFIG   the settings the libraries were built with
#
# MARCH and LIBC_SPECS are LIBRARY-FIXED: leave them unset or repeat the
# libraries' values. CROSS_COMPILE, BUILD_TYPE and LIBC_LINK_SPECS default to
# the libraries' values. MPFS_SMODE_LDSCRIPT replaces the installed linker script.

ifndef MPFS_SMODE_DIR
  $(error Set MPFS_SMODE_DIR to the installed libraries before including mpfs_smode.mk)
endif

include $(MPFS_SMODE_DIR)/mk/build_config.mk

CROSS_COMPILE   ?= $(MPFS_SMODE_BUILT_CROSS_COMPILE)
BUILD_TYPE      ?= $(MPFS_SMODE_BUILT_BUILD_TYPE)
LIBC_LINK_SPECS ?= $(MPFS_SMODE_BUILT_LIBC_LINK_SPECS)

mpfs_smode_fix = $(if $(filter undefined,$(origin $(1))),\
    $(eval $(1) := $(MPFS_SMODE_BUILT_$(1))),\
    $(if $(subst $(strip $($(1))),,$(strip $(MPFS_SMODE_BUILT_$(1))))$(subst $(strip $(MPFS_SMODE_BUILT_$(1))),,$(strip $($(1)))),\
        $(error $(1) is LIBRARY-FIXED: the libraries in $(MPFS_SMODE_DIR) were built with \
            $(1)=$(MPFS_SMODE_BUILT_$(1)), not $($(1)). Rebuild the libraries to change it)))
$(foreach v,MARCH LIBC_SPECS,$(call mpfs_smode_fix,$v))

include $(MPFS_SMODE_DIR)/mk/toolchain.mk

MPFS_SMODE_LDSCRIPT ?= $(MPFS_SMODE_DIR)/lib/ldscripts/linker.ld
MPFS_SMODE_CFLAGS  := $(MPFS_CFLAGS) -I$(MPFS_SMODE_DIR)/include -I$(MPFS_SMODE_DIR)/include/freertos
MPFS_SMODE_LDFLAGS := $(MPFS_LDFLAGS) -T$(MPFS_SMODE_LDSCRIPT)
MPFS_SMODE_LIBS    := -Wl,--whole-archive \
                      $(MPFS_SMODE_DIR)/lib/libmpfs_smode_boot.a \
                      $(MPFS_SMODE_DIR)/lib/libfreertos_smode.a \
                      -Wl,--no-whole-archive
MPFS_SMODE_CONFIG  := $(MPFS_SMODE_DIR)/mk/build_config.mk
MPFS_SMODE_DEPS    := $(MPFS_SMODE_DIR)/lib/libmpfs_smode_boot.a \
                      $(MPFS_SMODE_DIR)/lib/libfreertos_smode.a \
                      $(MPFS_SMODE_LDSCRIPT)
