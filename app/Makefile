# SPDX-License-Identifier: MIT
#
# The S-mode application (main.c), linked against the installed libraries in
# MPFS_SMODE_DIR. The top-level Makefile runs this after staging the
# libraries in build/dist; on its own:
#
#   make -f app/Makefile MPFS_SMODE_DIR=<prefix>     -> build/hello.elf, .hex, .map
#
# OUT_DIR (default build) is relative to the working directory.

APP_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
MPFS_SMODE_DIR ?= $(APP_DIR)/../build/dist
OUT_DIR        ?= build
APP            := hello

include $(MPFS_SMODE_DIR)/mpfs_smode.mk

SRCS    := main.c
OBJ_DIR := $(OUT_DIR)/obj/app
OBJS    := $(patsubst %.c,$(OBJ_DIR)/%.c.o,$(SRCS))
ELF     := $(OUT_DIR)/$(APP).elf

.PHONY: all clean
all: $(OUT_DIR)/$(APP).hex

$(OBJ_DIR)/%.c.o: $(APP_DIR)/%.c $(MPFS_SMODE_CONFIG)
	@mkdir -p $(@D)
	$(CC) $(MPFS_SMODE_CFLAGS) -Wall -Wextra -MMD -MP -c $< -o $@

$(ELF): $(OBJS) $(MPFS_SMODE_DEPS)
	$(CC) $(MPFS_SMODE_LDFLAGS) -Wl,-Map=$(OUT_DIR)/$(APP).map $(OBJS) $(MPFS_SMODE_LIBS) -o $@
	$(SIZE) $@

$(OUT_DIR)/$(APP).hex: $(ELF)
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(OBJ_DIR) $(ELF) $(OUT_DIR)/$(APP).hex $(OUT_DIR)/$(APP).map

-include $(OBJS:.o=.d)
