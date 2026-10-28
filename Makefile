# SPDX-License-Identifier: MIT
#
# Bare-bones S-mode FreeRTOS hello-world for PolarFire SoC U54_1: one ELF from
# the S-mode port (port/), the boot code (boot/), the application (app/) and
# the FreeRTOS kernel in FREERTOS_KERNEL_DIR. CMakeLists.txt builds the same.
#
#   make FREERTOS_KERNEL_DIR=<FreeRTOS-Kernel tree>     -> build/hello.elf, .hex, .map
#   make clean
#
# Variables:
#   FREERTOS_KERNEL_DIR  FreeRTOS-Kernel tree (required): its core .c files,
#                        include/ and portable/MemMang/heap_4.c are built
#   CROSS_COMPILE        default riscv64-unknown-elf-
#   BUILD_TYPE           Debug (-O0 -g3, default) or Release (-Os -g)
#   MARCH                default rv64gc, rv64gc_zicsr_zifencei on GCC >= 12
#   LIBC_SPECS           default --specs=nano.specs (picolibc: --specs=picolibc.specs)
#   LIBC_LINK_SPECS      default --specs=nosys.specs (picolibc: empty)
#   OUT_DIR              default build

ifeq ($(strip $(FREERTOS_KERNEL_DIR)),)
  $(error FREERTOS_KERNEL_DIR is not set: make FREERTOS_KERNEL_DIR=<path to a FreeRTOS-Kernel tree>)
endif
KERNEL := $(patsubst %/,%,$(FREERTOS_KERNEL_DIR))
ifneq ($(words $(wildcard $(KERNEL)/tasks.c $(KERNEL)/include/FreeRTOS.h)),2)
  $(error FREERTOS_KERNEL_DIR ($(KERNEL)) is not a FreeRTOS-Kernel tree (no tasks.c / include/FreeRTOS.h))
endif

CROSS_COMPILE   ?= riscv64-unknown-elf-
BUILD_TYPE      ?= Debug
LIBC_SPECS      ?= --specs=nano.specs
LIBC_LINK_SPECS ?= --specs=nosys.specs
OUT_DIR         ?= build

CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

GCC_MAJOR := $(firstword $(subst ., ,$(shell $(CC) -dumpversion)))
MARCH ?= $(if $(filter 1 2 3 4 5 6 7 8 9 10 11,$(GCC_MAJOR)),rv64gc,rv64gc_zicsr_zifencei)

OPT := $(if $(filter Release,$(BUILD_TYPE)),-Os -g,-O0 -g3)

CFLAGS := -march=$(MARCH) -mabi=lp64d -mcmodel=medany -mstrict-align -fno-jump-tables \
          -ffunction-sections -fdata-sections -fmessage-length=0 $(LIBC_SPECS) $(OPT) \
          -Iapp -Iboot -Iport/RISC-V-SMode -I$(KERNEL)/include
LDFLAGS := -march=$(MARCH) -mabi=lp64d -mcmodel=medany $(LIBC_SPECS) $(LIBC_LINK_SPECS) \
           -nostartfiles -Wl,--gc-sections -T boot/linker.ld

SRCS := boot/entry.S boot/m_trap.S boot/m_shim.c boot/s_start.S \
        port/RISC-V-SMode/port.c port/RISC-V-SMode/portASM.S \
        app/main.c
KERNEL_SRCS := $(addprefix $(KERNEL)/,list.c queue.c tasks.c portable/MemMang/heap_4.c)

OBJ_DIR := $(OUT_DIR)/obj
OBJS := $(patsubst %,$(OBJ_DIR)/%.o,$(SRCS)) \
        $(patsubst $(KERNEL)/%,$(OBJ_DIR)/kernel/%.o,$(KERNEL_SRCS))
ELF := $(OUT_DIR)/hello.elf

.PHONY: all clean
all: $(OUT_DIR)/hello.hex

$(OBJ_DIR)/kernel/%.c.o: $(KERNEL)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.o: %
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Wall -Wextra -MMD -MP -c $< -o $@

$(ELF): $(OBJS) boot/linker.ld
	$(CC) $(LDFLAGS) -Wl,-Map=$(OUT_DIR)/hello.map $(OBJS) -o $@
	$(SIZE) $@

$(OUT_DIR)/hello.hex: $(ELF)
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(OUT_DIR)

-include $(OBJS:.o=.d)
