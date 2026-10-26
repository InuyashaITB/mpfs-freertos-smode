# SPDX-License-Identifier: MIT
#
# PolarFire SoC: FreeRTOS in S-mode on U54_1, as static libraries.
# Plain GNU make; works from SoftConsole (Windows build_tools) and Linux.
#
#   libmpfs_smode_boot.a  MPFS HAL start-up + U54_1 M-mode set-up, M-mode SBI
#                         shim, Sv39 bring-up, S-mode entry -> s_mode_main()
#   libfreertos_smode.a   FreeRTOS kernel + S-mode port + trap glue, built
#                         against $(FREERTOS_CONFIG_DIR)/FreeRTOSConfig.h
#   libmpfs_drivers.a     MSS drivers listed in MPFS_DRIVERS (for applications)
#
# Targets:
#   make                      libraries, install tree in build/dist, demo
#                             (build/examples/freertos-demo/freertos-demo.elf)
#   make libs                 libraries + install tree only
#   make install PREFIX=dir   copy the install tree to dir (default: dist)
#   make clean                remove build/
#
# Variables: see mk/toolchain.mk (CROSS_COMPILE, BUILD_TYPE, LIBC_SPECS,
# LIBC_LINK_SPECS, MARCH, V) and below. Run 'make clean' after changing the
# toolchain variables. Every path is relative to this directory, so object
# and dependency files never contain drive letters.

FREERTOS_CONFIG_DIR ?= examples/freertos-demo
MPFS_BOARD_DIR      ?= src/boards/icicle-kit-es
MPFS_LINKER_SCRIPT  ?= $(MPFS_BOARD_DIR)/platform_config/linker/mpfs-lim-sv39.ld
MPFS_DRIVERS        ?= mss_mmuart mss_gpio
BUILD_DIR           ?= build
PREFIX              ?= dist

include mk/toolchain.mk

STAGE   := $(BUILD_DIR)/dist
OBJ_DIR := $(BUILD_DIR)/obj/$(BUILD_TYPE)

ifeq ($(wildcard $(FREERTOS_CONFIG_DIR)/FreeRTOSConfig.h),)
  $(error FREERTOS_CONFIG_DIR ($(FREERTOS_CONFIG_DIR)) has no FreeRTOSConfig.h)
endif

# Recursive wildcard: $(call rwildcard,dir,*.c *.S)
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

PLATFORM := src/platform
KERNEL   := src/middleware/FreeRTOS-Kernel
PORT     := $(KERNEL)/portable/GCC/RISC-V-SMode

#-------------------------------------------------------------------------------
# Sources
#-------------------------------------------------------------------------------
HAL_SRCS := $(call rwildcard,$(PLATFORM)/mpfs_hal,*.c *.S) \
            $(call rwildcard,$(PLATFORM)/hal,*.c *.S)

SMODE_SRCS := src/smode/u54_1_boot.c \
              src/smode/m_shim.c \
              src/smode/m_shim_trap.S \
              src/smode/smode_start.S \
              src/smode/smode_diag.c \
              src/smode/sv39.c

GLUE_SRCS := src/freertos_glue/smode_irq.c

KERNEL_SRCS := $(KERNEL)/croutine.c \
               $(KERNEL)/event_groups.c \
               $(KERNEL)/list.c \
               $(KERNEL)/queue.c \
               $(KERNEL)/stream_buffer.c \
               $(KERNEL)/tasks.c \
               $(KERNEL)/timers.c \
               $(KERNEL)/portable/MemMang/heap_4.c \
               $(PORT)/port.c \
               $(PORT)/portASM.S

DRIVER_SRCS := $(foreach d,$(MPFS_DRIVERS),$(call rwildcard,$(PLATFORM)/drivers/mss/$d,*.c *.S))
$(foreach d,$(MPFS_DRIVERS),$(if $(wildcard $(PLATFORM)/drivers/mss/$d),,\
    $(error MPFS_DRIVERS: no driver directory $(PLATFORM)/drivers/mss/$d)))

obj = $(patsubst %,$(OBJ_DIR)/%.o,$(1))

BOOT_OBJS     := $(call obj,$(HAL_SRCS) $(SMODE_SRCS))
FREERTOS_OBJS := $(call obj,$(KERNEL_SRCS) $(GLUE_SRCS))
DRIVER_OBJS   := $(call obj,$(DRIVER_SRCS))
ALL_OBJS      := $(BOOT_OBJS) $(FREERTOS_OBJS) $(DRIVER_OBJS)

# Header search paths, as seen by code: "mpfs_hal/...", "drivers/mss/...",
# "fpga_design_config/...", "mpfs_hal_config/...", "smode/...", "FreeRTOS.h".
LIB_INCLUDES := -I$(PLATFORM) \
                -I$(MPFS_BOARD_DIR) \
                -I$(MPFS_BOARD_DIR)/platform_config \
                -Isrc \
                -I$(FREERTOS_CONFIG_DIR) \
                -I$(KERNEL)/include \
                -I$(PORT)

# Our own sources get the extra warnings; vendor code builds as before.
$(call obj,$(SMODE_SRCS) $(GLUE_SRCS)): EXTRA_CFLAGS := -Wall -Wextra

#-------------------------------------------------------------------------------
# Libraries
#-------------------------------------------------------------------------------
BOOT_LIB     := $(STAGE)/lib/libmpfs_smode_boot.a
FREERTOS_LIB := $(STAGE)/lib/libfreertos_smode.a
DRIVERS_LIB  := $(STAGE)/lib/libmpfs_drivers.a
LIBS         := $(BOOT_LIB) $(FREERTOS_LIB) $(DRIVERS_LIB)

.PHONY: all libs stage demo install clean FORCE

all: demo

libs: stage

$(OBJ_DIR)/%.c.o: %.c
	@echo "CC  $<"
	@mkdir -p $(@D)
	$(Q)$(CC) $(MPFS_CFLAGS) $(EXTRA_CFLAGS) $(LIB_INCLUDES) -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.S.o: %.S
	@echo "AS  $<"
	@mkdir -p $(@D)
	$(Q)$(CC) $(MPFS_CFLAGS) $(EXTRA_CFLAGS) $(LIB_INCLUDES) -MMD -MP -c $< -o $@

# 'q' (append) rather than 'r': members are stored by base name, and 'r'
# would replace one of two sources that share a name.
$(BOOT_LIB): $(BOOT_OBJS)
$(FREERTOS_LIB): $(FREERTOS_OBJS)
$(DRIVERS_LIB): $(DRIVER_OBJS)
$(LIBS):
	@echo "AR  $@"
	@mkdir -p $(@D)
	@rm -f $@
	$(Q)$(AR) qcs $@ $^

-include $(ALL_OBJS:.o=.d)

#-------------------------------------------------------------------------------
# Install tree ($(STAGE)): what 'make install' ships and what the demo uses
#
#   lib/*.a, lib/ldscripts/mpfs-lim-sv39.ld
#   include/{mpfs_hal,hal,drivers,fpga_design_config,mpfs_hal_config,smode}/
#   include/freertos/       kernel + port headers, the FreeRTOSConfig.h used
#   mpfs_smode.mk, mk/toolchain.mk, mk/build_config.mk
#-------------------------------------------------------------------------------
STAGED :=

# $(call stage,dest-dir,source-files,source-root): one copy rule per file.
define COPY_RULE
STAGED += $(1)
$(1): $(2)
	@mkdir -p $$(@D)
	$$(Q)cp $$< $$@
endef
stage = $(foreach f,$(2),$(eval $(call COPY_RULE,$(STAGE)$(if $(1),/$(1))/$(patsubst $(3)/%,%,$(f)),$(f))))

PLATFORM_HDRS := $(filter-out $(PLATFORM)/platform_config_reference/% $(PLATFORM)/soc_config_generator/%,\
                   $(call rwildcard,$(PLATFORM),*.h))

$(call stage,include,$(PLATFORM_HDRS),$(PLATFORM))
$(call stage,include/fpga_design_config,$(call rwildcard,$(MPFS_BOARD_DIR)/fpga_design_config,*.h),$(MPFS_BOARD_DIR)/fpga_design_config)
$(call stage,include/mpfs_hal_config,$(wildcard $(MPFS_BOARD_DIR)/platform_config/mpfs_hal_config/*.h),$(MPFS_BOARD_DIR)/platform_config/mpfs_hal_config)
$(call stage,include/smode,$(wildcard src/smode/*.h),src/smode)
$(call stage,include/freertos,$(wildcard $(KERNEL)/include/*.h),$(KERNEL)/include)
$(call stage,include/freertos,$(PORT)/portmacro.h $(PORT)/portContext.h $(PORT)/freertos_risc_v_chip_specific_extensions.h,$(PORT))
$(call stage,include/freertos,$(FREERTOS_CONFIG_DIR)/FreeRTOSConfig.h,$(FREERTOS_CONFIG_DIR))
$(call stage,lib/ldscripts,$(MPFS_LINKER_SCRIPT),$(patsubst %/,%,$(dir $(MPFS_LINKER_SCRIPT))))
$(call stage,,mk/mpfs_smode.mk,mk)
$(call stage,mk,mk/toolchain.mk,mk)

# Defaults for applications: the toolchain settings the libraries were built
# with. Rewritten on every build (cheap, nothing depends on its time stamp).
BUILD_CONFIG := $(STAGE)/mk/build_config.mk
$(BUILD_CONFIG): FORCE
	@mkdir -p $(@D)
	@echo "# Generated by the mpfs-freertos-smode Makefile: settings the libraries were built with." > $@
	@echo "CROSS_COMPILE ?= $(CROSS_COMPILE)" >> $@
	@echo "BUILD_TYPE ?= $(BUILD_TYPE)" >> $@
	@echo "LIBC_SPECS ?= $(LIBC_SPECS)" >> $@
	@echo "LIBC_LINK_SPECS ?= $(LIBC_LINK_SPECS)" >> $@
	@echo "MARCH ?= $(MARCH)" >> $@

stage: $(LIBS) $(STAGED) $(BUILD_CONFIG)

#-------------------------------------------------------------------------------
# Demo application, built only from the install tree like any external app
#-------------------------------------------------------------------------------
demo: stage
	$(Q)$(MAKE) --no-print-directory -f examples/freertos-demo/Makefile \
	    MPFS_SMODE_DIR=$(STAGE) OUT_DIR=$(BUILD_DIR)/examples/freertos-demo

install: stage
	@mkdir -p $(PREFIX)
	cp -r $(STAGE)/. $(PREFIX)/

clean:
	rm -rf $(BUILD_DIR)

FORCE:
