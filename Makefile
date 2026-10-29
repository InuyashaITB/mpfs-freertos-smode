# SPDX-License-Identifier: MIT
#
# Bare-bones S-mode FreeRTOS for PolarFire SoC U54_1: two static libraries,
# staged as an installable package in DIST_DIR, then the application
# (app/Makefile) linked against that package. CMakeLists.txt builds the same.
#
#   libmpfs_smode_boot.a  boot/: M-mode _start, delegation and PMP, the SBI
#                         shim, the M->S handoff. No FreeRTOS.
#   libfreertos_smode.a   the FreeRTOS kernel and the S-mode port (port/),
#                         built against FREERTOS_CONFIG_DIR/FreeRTOSConfig.h
#
#   make FREERTOS_KERNEL_DIR=<FreeRTOS-Kernel tree>    -> build/dist, build/hello.elf, .hex, .map
#   make lib FREERTOS_KERNEL_DIR=<...>                 -> build/dist only
#   make install PREFIX=<dir> FREERTOS_KERNEL_DIR=<...>
#   make clean
#
# Variables:
#   FREERTOS_KERNEL_DIR  FreeRTOS-Kernel tree (required): its core .c files,
#                        include/ and portable/MemMang/heap_4.c are built
#   FREERTOS_CONFIG_DIR  default config: the FreeRTOSConfig.h built into
#                        libfreertos_smode (and installed with it)
#   CROSS_COMPILE, BUILD_TYPE, MARCH, LIBC_SPECS, LIBC_LINK_SPECS: mk/toolchain.mk
#   OUT_DIR              default build
#   DIST_DIR             default $(OUT_DIR)/dist

OUT_DIR             ?= build
DIST_DIR            ?= $(OUT_DIR)/dist
FREERTOS_CONFIG_DIR ?= config

ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
  ifeq ($(strip $(FREERTOS_KERNEL_DIR)),)
    $(error FREERTOS_KERNEL_DIR is not set: make FREERTOS_KERNEL_DIR=<path to a FreeRTOS-Kernel tree>)
  endif
  KERNEL := $(patsubst %/,%,$(FREERTOS_KERNEL_DIR))
  ifneq ($(words $(wildcard $(KERNEL)/tasks.c $(KERNEL)/include/FreeRTOS.h)),2)
    $(error FREERTOS_KERNEL_DIR ($(KERNEL)) is not a FreeRTOS-Kernel tree (no tasks.c / include/FreeRTOS.h))
  endif
endif
CONFIG := $(patsubst %/,%,$(FREERTOS_CONFIG_DIR))
ifeq ($(wildcard $(CONFIG)/FreeRTOSConfig.h),)
  $(error FREERTOS_CONFIG_DIR ($(CONFIG)) has no FreeRTOSConfig.h)
endif

include mk/toolchain.mk

PORT    := port/RISC-V-SMode
LIB_OBJ := $(OUT_DIR)/obj/lib

BOOT_SRCS   := boot/entry.S boot/m_trap.S boot/m_shim.c boot/s_start.S
PORT_SRCS   := $(PORT)/port.c $(PORT)/portASM.S
KERNEL_SRCS := list.c queue.c tasks.c portable/MemMang/heap_4.c

BOOT_OBJS     := $(patsubst %,$(LIB_OBJ)/%.o,$(BOOT_SRCS))
FREERTOS_OBJS := $(patsubst %,$(LIB_OBJ)/%.o,$(PORT_SRCS)) \
                 $(patsubst %,$(LIB_OBJ)/kernel/%.o,$(KERNEL_SRCS))

BOOT_CFLAGS     := $(MPFS_CFLAGS) -Iboot
FREERTOS_CFLAGS := $(MPFS_CFLAGS) -I$(CONFIG) -I$(KERNEL)/include -I$(PORT)

BOOT_LIB     := $(DIST_DIR)/lib/libmpfs_smode_boot.a
FREERTOS_LIB := $(DIST_DIR)/lib/libfreertos_smode.a

# The package: the same layout as cmake --install.
STAGED := $(DIST_DIR)/include/mpfs.h \
          $(patsubst $(KERNEL)/include/%,$(DIST_DIR)/include/freertos/%,$(wildcard $(KERNEL)/include/*.h)) \
          $(addprefix $(DIST_DIR)/include/freertos/,portmacro.h portContext.h \
              freertos_risc_v_chip_specific_extensions.h FreeRTOSConfig.h) \
          $(DIST_DIR)/lib/ldscripts/linker.ld \
          $(DIST_DIR)/mpfs_smode.mk $(DIST_DIR)/mk/toolchain.mk \
          $(addprefix $(DIST_DIR)/lib/cmake/MpfsSmode/,MpfsSmodeConfig.cmake MpfsSmodeFlags.cmake \
              riscv64-unknown-elf.cmake)
BUILD_CONFIG       := $(DIST_DIR)/mk/build_config.mk
BUILD_CONFIG_CMAKE := $(DIST_DIR)/lib/cmake/MpfsSmode/build_config.cmake
SETTINGS           := CROSS_COMPILE BUILD_TYPE MARCH LIBC_SPECS LIBC_LINK_SPECS

.PHONY: all lib app install clean FORCE
all: app

lib: $(BOOT_LIB) $(FREERTOS_LIB) $(STAGED) $(BUILD_CONFIG) $(BUILD_CONFIG_CMAKE)

app: lib
	"$(MAKE)" -f app/Makefile MPFS_SMODE_DIR=$(DIST_DIR) OUT_DIR=$(OUT_DIR)

install: lib
ifeq ($(strip $(PREFIX)),)
	$(error Set PREFIX to the install directory, e.g. make install PREFIX=C:/mpfs-smode)
endif
	mkdir -p $(PREFIX)
	cp -R $(DIST_DIR)/. $(PREFIX)/

$(LIB_OBJ)/boot/%.o: boot/%
	@mkdir -p $(@D)
	$(CC) $(BOOT_CFLAGS) -Wall -Wextra -MMD -MP -c $< -o $@

$(LIB_OBJ)/$(PORT)/%.o: $(PORT)/%
	@mkdir -p $(@D)
	$(CC) $(FREERTOS_CFLAGS) -Wall -Wextra -MMD -MP -c $< -o $@

$(LIB_OBJ)/kernel/%.o: $(KERNEL)/%
	@mkdir -p $(@D)
	$(CC) $(FREERTOS_CFLAGS) -MMD -MP -c $< -o $@

$(BOOT_LIB): $(BOOT_OBJS)
$(FREERTOS_LIB): $(FREERTOS_OBJS)
$(BOOT_LIB) $(FREERTOS_LIB):
	@mkdir -p $(@D)
	rm -f $@
	$(AR) qcs $@ $^

# Staging: the port's headers first, so the kernel's rule only gets its own.
$(DIST_DIR)/include/%.h: boot/%.h
	@mkdir -p $(@D)
	cp $< $@
$(DIST_DIR)/include/freertos/FreeRTOSConfig.h: $(CONFIG)/FreeRTOSConfig.h
	@mkdir -p $(@D)
	cp $< $@
$(DIST_DIR)/include/freertos/%.h: $(PORT)/%.h
	@mkdir -p $(@D)
	cp $< $@
$(DIST_DIR)/include/freertos/%.h: $(KERNEL)/include/%.h
	@mkdir -p $(@D)
	cp $< $@
$(DIST_DIR)/lib/ldscripts/linker.ld: boot/linker.ld
	@mkdir -p $(@D)
	cp $< $@
$(DIST_DIR)/mpfs_smode.mk: mk/mpfs_smode.mk
	@mkdir -p $(@D)
	cp $< $@
$(DIST_DIR)/mk/toolchain.mk: mk/toolchain.mk
	@mkdir -p $(@D)
	cp $< $@
$(DIST_DIR)/lib/cmake/MpfsSmode/%.cmake: cmake/%.cmake
	@mkdir -p $(@D)
	cp $< $@

# The settings the libraries were built with: rewritten only when they change,
# so the application rebuilds exactly when it must.
$(BUILD_CONFIG): FORCE
	@mkdir -p $(@D)
	@echo '# Generated by the mpfs-freertos-smode build: the settings the libraries were built with.' > $@.tmp
	@$(foreach v,$(SETTINGS),echo 'MPFS_SMODE_BUILT_$(v) := $($(v))' >> $@.tmp;)
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@
$(BUILD_CONFIG_CMAKE): FORCE
	@mkdir -p $(@D)
	@echo '# Generated by the mpfs-freertos-smode build: the settings the libraries were built with.' > $@.tmp
	@$(foreach v,$(SETTINGS),echo 'set(MPFS_SMODE_BUILT_$(v) "$($(v))")' >> $@.tmp;)
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

clean:
	rm -rf $(OUT_DIR)

-include $(BOOT_OBJS:.o=.d) $(FREERTOS_OBJS:.o=.d)
