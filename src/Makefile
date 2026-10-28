# SPDX-License-Identifier: MIT
#
# Library build: PolarFire SoC FreeRTOS in S-mode on U54_1, as static
# libraries plus an install tree. Knows nothing about any application.
# src/CMakeLists.txt is the CMake equivalent: keep the explicit source lists
# below in sync with it.
# Plain GNU make; works from SoftConsole (Windows build_tools) and Linux.
#
#   libmpfs_smode_boot.a  MPFS HAL start-up + U54_1 M-mode set-up, M-mode SBI
#                         shim, Sv39 bring-up, S-mode entry -> s_mode_main()
#   libfreertos_smode.a   FreeRTOS kernel + S-mode port + trap glue, built
#                         against $(FREERTOS_CONFIG_DIR)/FreeRTOSConfig.h
#   libmpfs_drivers.a     MSS drivers listed in MPFS_DRIVERS (for applications)
#   libfreertos_tcp.a     FreeRTOS+TCP + the LiteEth network interface, built
#                         against $(FREERTOS_IP_CONFIG_DIR)/FreeRTOSIPConfig.h
#                         (for applications that use networking)
#
# Run in this directory ('make -C src' from the project root):
#   make                      libraries + install tree in DIST_DIR
#   make install PREFIX=dir   copy the install tree to dir
#   make clean                remove OUT_DIR and DIST_DIR
#
# Variables: see mk/toolchain.mk (CROSS_COMPILE, BUILD_TYPE, LIBC_SPECS,
# LIBC_LINK_SPECS, MARCH, V) and below. Paths are relative to this
# directory. Run 'make clean' after changing the toolchain variables. Every
# path is kept relative, so object and dependency files never contain drive
# letters.
#
#   FREERTOS_CONFIG_DIR  FreeRTOSConfig.h compiled into libfreertos_smode and
#                        installed with it. Default: freertos_glue/config,
#                        the library's reference configuration
#   FREERTOS_KERNEL_DIR  FreeRTOS-Kernel tree whose core sources, include/
#                        and heap_4.c libfreertos_smode is built from.
#                        Default: middleware/FreeRTOS-Kernel (V11.3.2). The
#                        S-mode port always comes from the bundled tree; see
#                        middleware/README.md, "Older FreeRTOS kernels"
#   FREERTOS_IP_CONFIG_DIR  FreeRTOSIPConfig.h compiled into libfreertos_tcp
#                        and installed with it. Default: freertos_tcp/config
#   MPFS_BOARD_DIR       board configuration headers
#   MPFS_LINKER_SCRIPT   linker script installed for applications
#   MPFS_DRIVERS         drivers/mss/<name> built into libmpfs_drivers
#   OUT_DIR              objects                ../build/lib
#   DIST_DIR             install tree           ../build/dist

FREERTOS_CONFIG_DIR ?= freertos_glue/config
FREERTOS_KERNEL_DIR ?= middleware/FreeRTOS-Kernel
FREERTOS_IP_CONFIG_DIR ?= freertos_tcp/config
MPFS_BOARD_DIR      ?= boards/icicle-kit-es
MPFS_LINKER_SCRIPT  ?= $(MPFS_BOARD_DIR)/platform_config/linker/mpfs-lim-sv39.ld
MPFS_DRIVERS        ?= mss_mmuart mss_gpio mss_spi mss_i2c
OUT_DIR             ?= ../build/lib
DIST_DIR            ?= ../build/dist

include mk/toolchain.mk

STAGE   := $(DIST_DIR)
OBJ_DIR := $(OUT_DIR)/obj/$(BUILD_TYPE)

# Check the paths first: a bad one must not wipe the previous build below.
ifeq ($(wildcard $(FREERTOS_CONFIG_DIR)/FreeRTOSConfig.h),)
  $(error FREERTOS_CONFIG_DIR ($(FREERTOS_CONFIG_DIR)) has no FreeRTOSConfig.h)
endif
ifneq ($(words $(wildcard $(FREERTOS_KERNEL_DIR)/tasks.c $(FREERTOS_KERNEL_DIR)/include/FreeRTOS.h)),2)
  $(error FREERTOS_KERNEL_DIR ($(FREERTOS_KERNEL_DIR)) is not a FreeRTOS-Kernel tree (no tasks.c / include/FreeRTOS.h))
endif
ifeq ($(wildcard $(FREERTOS_IP_CONFIG_DIR)/FreeRTOSIPConfig.h),)
  $(error FREERTOS_IP_CONFIG_DIR ($(FREERTOS_IP_CONFIG_DIR)) has no FreeRTOSIPConfig.h)
endif

# Objects only track sources and headers, not flags. The install tree records
# the settings of the last build (mk/build_config.mk); when any of them
# differs now, remove the previous objects and install tree and build from
# scratch. The file is regenerated (and make restarts) only in that case, so
# its time stamp tells applications when the libraries' settings changed.
BUILD_CONFIG       := $(STAGE)/mk/build_config.mk
BUILD_CONFIG_CMAKE := $(STAGE)/lib/cmake/MpfsSmode/build_config.cmake
-include $(BUILD_CONFIG)
MPFS_SETTINGS_CHANGED := $(strip $(foreach v,$(MPFS_SMODE_SETTINGS),\
    $(if $(call mpfs_differs,$($v),$(MPFS_SMODE_BUILT_$v)),$v)))
ifneq ($(MPFS_SETTINGS_CHANGED),)
  ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
    ifneq ($(wildcard $(BUILD_CONFIG)),)
      $(info Library settings changed ($(MPFS_SETTINGS_CHANGED)): rebuilding the libraries from scratch)
      $(shell rm -rf $(OUT_DIR) $(STAGE))
    endif
  endif
endif


# Recursive wildcard: $(call rwildcard,dir,*.c *.S)
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

PLATFORM := platform
# Kernel core from FREERTOS_KERNEL_DIR; the S-mode port is always the bundled one.
KERNEL   := $(patsubst %/,%,$(FREERTOS_KERNEL_DIR))
PORT     := middleware/FreeRTOS-Kernel/portable/GCC/RISC-V-SMode
TCP      := middleware/FreeRTOS-Plus-TCP/source

#-------------------------------------------------------------------------------
# Sources
#-------------------------------------------------------------------------------
HAL_SRCS := $(call rwildcard,$(PLATFORM)/mpfs_hal,*.c *.S) \
            $(call rwildcard,$(PLATFORM)/hal,*.c *.S)

SMODE_SRCS := smode/u54_1_boot.c \
              smode/m_shim.c \
              smode/m_shim_trap.S \
              smode/smode_start.S \
              smode/smode_diag.c \
              smode/sv39.c

GLUE_SRCS := freertos_glue/smode_irq.c

KERNEL_SRCS := $(KERNEL)/croutine.c \
               $(KERNEL)/event_groups.c \
               $(KERNEL)/list.c \
               $(KERNEL)/queue.c \
               $(KERNEL)/stream_buffer.c \
               $(KERNEL)/tasks.c \
               $(KERNEL)/timers.c \
               $(KERNEL)/portable/MemMang/heap_4.c
PORT_SRCS   := $(PORT)/port.c \
               $(PORT)/portASM.S

# FreeRTOS+TCP: every stack source (FreeRTOSIPConfig.h decides what each one
# compiles to), the heap-based buffer allocator, and our LiteEth interface.
TCP_STACK_SRCS := $(sort $(wildcard $(TCP)/*.c)) \
                  $(TCP)/portable/BufferManagement/BufferAllocation_2.c
TCP_NETIF_SRCS := freertos_tcp/liteeth.c \
                  freertos_tcp/NetworkInterface_LiteEth.c

DRIVER_SRCS := $(foreach d,$(MPFS_DRIVERS),$(call rwildcard,$(PLATFORM)/drivers/mss/$d,*.c *.S))
$(foreach d,$(MPFS_DRIVERS),$(if $(wildcard $(PLATFORM)/drivers/mss/$d),,\
    $(error MPFS_DRIVERS: no driver directory $(PLATFORM)/drivers/mss/$d)))

obj = $(patsubst %,$(OBJ_DIR)/%.o,$(1))

BOOT_OBJS     := $(call obj,$(HAL_SRCS) $(SMODE_SRCS))
# Kernel objects under obj/kernel/: FREERTOS_KERNEL_DIR may be absolute (or
# a drive path on Windows), which cannot be appended to OBJ_DIR.
KERNEL_OBJS   := $(patsubst $(KERNEL)/%,$(OBJ_DIR)/kernel/%.o,$(KERNEL_SRCS))
FREERTOS_OBJS := $(KERNEL_OBJS) $(call obj,$(PORT_SRCS) $(GLUE_SRCS))
DRIVER_OBJS   := $(call obj,$(DRIVER_SRCS))
TCP_OBJS      := $(call obj,$(TCP_STACK_SRCS) $(TCP_NETIF_SRCS))
ALL_OBJS      := $(BOOT_OBJS) $(FREERTOS_OBJS) $(DRIVER_OBJS) $(TCP_OBJS)

# Header search paths, as seen by code: "mpfs_hal/...", "drivers/mss/...",
# "fpga_design_config/...", "mpfs_hal_config/...", "smode/...", "FreeRTOS.h".
LIB_INCLUDES := -I$(PLATFORM) \
                -I$(MPFS_BOARD_DIR) \
                -I$(MPFS_BOARD_DIR)/platform_config \
                -I. \
                -I$(FREERTOS_CONFIG_DIR) \
                -I$(KERNEL)/include \
                -I$(PORT)

# FreeRTOS+TCP headers, as seen by code: "FreeRTOS_IP.h", "FreeRTOSIPConfig.h",
# "pack_struct_start.h", "NetworkInterface_LiteEth.h".
TCP_INCLUDES := -I$(FREERTOS_IP_CONFIG_DIR) \
                -I$(TCP)/include \
                -I$(TCP)/portable/Compiler/GCC \
                -Ifreertos_tcp
$(TCP_OBJS): LIB_INCLUDES += $(TCP_INCLUDES)

# Our own sources get the extra warnings; vendor code builds as before.
$(call obj,$(SMODE_SRCS) $(GLUE_SRCS) $(TCP_NETIF_SRCS)): EXTRA_CFLAGS := -Wall -Wextra

#-------------------------------------------------------------------------------
# Libraries
#-------------------------------------------------------------------------------
BOOT_LIB     := $(STAGE)/lib/libmpfs_smode_boot.a
FREERTOS_LIB := $(STAGE)/lib/libfreertos_smode.a
DRIVERS_LIB  := $(STAGE)/lib/libmpfs_drivers.a
TCP_LIB      := $(STAGE)/lib/libfreertos_tcp.a
LIBS         := $(BOOT_LIB) $(FREERTOS_LIB) $(DRIVERS_LIB) $(TCP_LIB)

.PHONY: all stage install clean FORCE

all: stage

$(OBJ_DIR)/%.c.o: %.c
	@echo "CC  $<"
	@mkdir -p $(@D)
	$(Q)$(CC) $(MPFS_CFLAGS) $(EXTRA_CFLAGS) $(LIB_INCLUDES) -MMD -MP -c $< -o $@

$(OBJ_DIR)/kernel/%.c.o: $(KERNEL)/%.c
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
$(TCP_LIB): $(TCP_OBJS)
$(LIBS):
	@echo "AR  $@"
	@mkdir -p $(@D)
	@rm -f $@
	$(Q)$(AR) qcs $@ $^

-include $(ALL_OBJS:.o=.d)

#-------------------------------------------------------------------------------
# Install tree ($(STAGE)): what 'make install' ships and what applications use
#
#   lib/*.a, lib/ldscripts/mpfs-lim-sv39.ld
#   include/{mpfs_hal,hal,drivers,fpga_design_config,mpfs_hal_config,smode}/
#   include/freertos/       kernel + port headers, the FreeRTOSConfig.h used
#   include/freertos_tcp/   FreeRTOS+TCP + LiteEth interface headers, the
#                           FreeRTOSIPConfig.h used
#   mpfs_smode.mk, mk/toolchain.mk, mk/build_config.mk          (make apps)
#   lib/cmake/MpfsSmode/                                        (CMake apps)
#-------------------------------------------------------------------------------
STAGED :=

# $(call stage,dest-dir,source-files,source-root): one copy rule per file.
define COPY_RULE
STAGED += $(1)
$(1): $(2)
	@mkdir -p $$(@D)
	$$(Q)$$(CP) $$< $$@
endef
stage = $(foreach f,$(2),$(eval $(call COPY_RULE,$(STAGE)$(if $(1),/$(1))/$(patsubst $(3)/%,%,$(f)),$(f))))

PLATFORM_HDRS := $(call rwildcard,$(PLATFORM),*.h)

$(call stage,include,$(PLATFORM_HDRS),$(PLATFORM))
$(call stage,include/fpga_design_config,$(call rwildcard,$(MPFS_BOARD_DIR)/fpga_design_config,*.h),$(MPFS_BOARD_DIR)/fpga_design_config)
$(call stage,include/mpfs_hal_config,$(wildcard $(MPFS_BOARD_DIR)/platform_config/mpfs_hal_config/*.h),$(MPFS_BOARD_DIR)/platform_config/mpfs_hal_config)
$(call stage,include/smode,$(wildcard smode/*.h),smode)
$(call stage,include/freertos,$(wildcard $(KERNEL)/include/*.h),$(KERNEL)/include)
$(call stage,include/freertos,$(PORT)/portmacro.h $(PORT)/portContext.h $(PORT)/freertos_risc_v_chip_specific_extensions.h,$(PORT))
$(call stage,include/freertos,$(FREERTOS_CONFIG_DIR)/FreeRTOSConfig.h,$(FREERTOS_CONFIG_DIR))
$(call stage,include/freertos_tcp,$(wildcard $(TCP)/include/*.h),$(TCP)/include)
$(call stage,include/freertos_tcp,$(wildcard $(TCP)/portable/Compiler/GCC/*.h),$(TCP)/portable/Compiler/GCC)
$(call stage,include/freertos_tcp,freertos_tcp/liteeth.h freertos_tcp/NetworkInterface_LiteEth.h,freertos_tcp)
$(call stage,include/freertos_tcp,$(FREERTOS_IP_CONFIG_DIR)/FreeRTOSIPConfig.h,$(FREERTOS_IP_CONFIG_DIR))
$(call stage,lib/ldscripts,$(MPFS_LINKER_SCRIPT),$(patsubst %/,%,$(dir $(MPFS_LINKER_SCRIPT))))
$(call stage,,mk/mpfs_smode.mk,mk)
$(call stage,mk,mk/toolchain.mk,mk)
$(call stage,lib/cmake/MpfsSmode,cmake/MpfsSmodeConfig.cmake cmake/MpfsSmodeFlags.cmake cmake/riscv64-unknown-elf.cmake,cmake)

# The settings the libraries were built with (see MPFS_SETTINGS_CHANGED
# above): read by mpfs_smode.mk (make applications) and by
# MpfsSmodeConfig.cmake (CMake applications), which stop if an application
# changes a LIBRARY-FIXED one. Written only when missing or changed.
# A newline, to put one command per setting in the recipes below.
define nl


endef

MPFS_CONFIG_FORCE := $(if $(or $(MPFS_SETTINGS_CHANGED),$(if $(wildcard $(BUILD_CONFIG)),,missing)),FORCE)

$(BUILD_CONFIG): $(MPFS_CONFIG_FORCE)
	@mkdir -p $(@D)
	@echo "# Generated by the mpfs-freertos-smode build: settings the libraries were built with." > $@
	@echo "# Do not edit; rebuild the libraries to change them." >> $@
	$(foreach v,$(MPFS_SMODE_SETTINGS),@echo "MPFS_SMODE_BUILT_$v := $(strip $($v))" >> $@$(nl))

$(BUILD_CONFIG_CMAKE): $(MPFS_CONFIG_FORCE)
	@mkdir -p $(@D)
	@echo "# Generated by the mpfs-freertos-smode build: settings the libraries were built with." > $@
	@echo "# Do not edit; rebuild the libraries to change them." >> $@
	$(foreach v,$(MPFS_SMODE_SETTINGS),@echo 'set(MPFS_SMODE_BUILT_$v "$(strip $($v))")' >> $@$(nl))
	@echo 'set(MPFS_SMODE_BOOT_DEFS "$(MPFS_BOOT_DEFS)")' >> $@

stage: $(LIBS) $(STAGED) $(BUILD_CONFIG) $(BUILD_CONFIG_CMAKE)

install: stage
ifndef PREFIX
	$(error Set PREFIX to the install directory, e.g. make install PREFIX=C:/mpfs-smode)
endif
	@mkdir -p $(PREFIX)
	$(CP) -r $(STAGE)/. $(PREFIX)/

clean:
	rm -rf $(OUT_DIR) $(STAGE)

FORCE:
