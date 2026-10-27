# SPDX-License-Identifier: MIT
#
# Application build: the FreeRTOS S-mode demo. Also a template for new
# applications.
#
# It only consumes an installed copy of the libraries (MPFS_SMODE_DIR: the
# directory 'make -C src install PREFIX=...' created, or build/dist). It never
# builds the libraries or looks at their sources. Run in this directory:
#
#   make MPFS_SMODE_DIR=<install prefix>
#   make clean
#
# MPFS_SMODE_DIR  installed libraries (default: ../../build/dist)
# OUT_DIR         output directory    (default: build)
#
# FreeRTOSConfig.h: the configuration is compiled into libfreertos_smode, and
# "FreeRTOS.h" includes the copy installed next to it. The one in this
# directory only takes effect if the libraries are built with it
# (FREERTOS_CONFIG_DIR, which the project's top-level Makefile passes).

APP_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

MPFS_SMODE_DIR ?= $(APP_DIR)/../../build/dist
OUT_DIR        ?= build
APP            := freertos-demo

include $(MPFS_SMODE_DIR)/mpfs_smode.mk

SRCS := app_main.c m_prehook.c other_harts.c ddr_buffers.c periph_selftest.c

# Implicit linker scripts added to the library's mpfs-lim-sv39.ld: the demo's
# layout of the DDR window (g_ddr_buffer_0..2).
APP_LDSCRIPTS := $(APP_DIR)/ddr_buffers.ld

OBJ_DIR := $(OUT_DIR)/obj/$(BUILD_TYPE)
OBJS    := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
ELF     := $(OUT_DIR)/$(APP).elf

.PHONY: all clean
all: $(OUT_DIR)/$(APP).hex

clean:
	rm -rf $(OUT_DIR)

$(OBJ_DIR)/%.o: $(APP_DIR)/%.c $(MPFS_SMODE_CONFIG)
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
