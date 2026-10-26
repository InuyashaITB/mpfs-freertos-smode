# SPDX-License-Identifier: MIT
#
# FreeRTOS S-mode demo, built against an installed copy of the libraries.
# Also a template for new applications.
#
# Built by the top-level Makefile ('make' in the project root), or on its own:
#
#   make -f examples/freertos-demo/Makefile MPFS_SMODE_DIR=<install prefix>
#
# MPFS_SMODE_DIR  installed libraries (default: ../../build/dist, i.e. the
#                 install tree of this project's build)
# OUT_DIR         output directory (default: build)
#
# The FreeRTOSConfig.h in this directory is the one the top-level build uses
# for libfreertos_smode by default (FREERTOS_CONFIG_DIR). The copy installed
# with the libraries is the one that counts: "FreeRTOS.h" includes the
# FreeRTOSConfig.h next to it.

APP_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

MPFS_SMODE_DIR ?= $(APP_DIR)/../../build/dist
OUT_DIR        ?= build
APP            := freertos-demo

include $(MPFS_SMODE_DIR)/mpfs_smode.mk

SRCS := app_main.c m_prehook.c other_harts.c ddr_buffers.c

# Implicit linker scripts added to the library's mpfs-lim-sv39.ld: the demo's
# layout of the DDR window (g_ddr_buffer_0..2).
APP_LDSCRIPTS := $(APP_DIR)/ddr_buffers.ld

OBJ_DIR := $(OUT_DIR)/obj/$(BUILD_TYPE)
OBJS    := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
ELF     := $(OUT_DIR)/$(APP).elf

.PHONY: all
all: $(OUT_DIR)/$(APP).hex

$(OBJ_DIR)/%.o: $(APP_DIR)/%.c
	@echo "CC  $<"
	@mkdir -p $(@D)
	$(Q)$(CC) $(MPFS_SMODE_CFLAGS) -Wall -Wextra -MMD -MP -c $< -o $@

$(ELF): $(OBJS) $(APP_LDSCRIPTS) $(MPFS_SMODE_DEPS)
	@echo "LD  $@"
	$(Q)$(CC) $(MPFS_SMODE_LDFLAGS) -Wl,-Map=$(OUT_DIR)/$(APP).map $(OBJS) $(APP_LDSCRIPTS) $(MPFS_SMODE_LIBS) -o $@
	$(Q)$(SIZE) --format=sysv --radix=16 $@

$(OUT_DIR)/$(APP).hex: $(ELF)
	$(Q)$(OBJCOPY) -O ihex $< $@

-include $(OBJS:.o=.d)
