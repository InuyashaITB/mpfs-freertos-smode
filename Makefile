# SPDX-License-Identifier: MIT
#
# Project orchestrator (what SoftConsole runs). It holds no build rules of
# its own: it builds the library, then the application against the
# library's install tree, each with its own Makefile.
#
#   src/Makefile                     library: libmpfs_smode_boot.a,
#                                    libfreertos_smode.a, libmpfs_drivers.a
#                                    + install tree build/dist
#   examples/freertos-demo/Makefile  application: consumes build/dist only
#
# Targets:
#   make                      library, then the demo
#                             (build/examples/freertos-demo/freertos-demo.elf)
#   make libs                 library + install tree only
#   make install PREFIX=dir   copy the install tree to dir
#   make clean                remove build/
#   make sim RENODE=<renode>  build, run the demo headless in Renode and check
#                             its UART output (renode/run-demo.resc,
#                             renode/expected-uart.txt); see renode/README.md
#
# Variables given here (BUILD_TYPE, CROSS_COMPILE, ...) are passed on to both
# Makefiles; see src/mk/toolchain.mk.

LIB_DIR := src
APP_DIR := examples/freertos-demo

# The demo's FreeRTOSConfig.h is compiled into the library (relative to src/).
LIB_ARGS := FREERTOS_CONFIG_DIR=../$(APP_DIR)
APP_ARGS := MPFS_SMODE_DIR=../../build/dist OUT_DIR=../../build/$(APP_DIR)

# PREFIX as given here, made usable from src/ (absolute paths kept).
INSTALL_PREFIX = $(if $(filter /% %:%,$(subst \,/,$(PREFIX))),$(PREFIX),../$(PREFIX))

.PHONY: all libs app install clean sim

all: app

libs:
	$(MAKE) -C $(LIB_DIR) $(LIB_ARGS)

app: libs
	$(MAKE) -C $(APP_DIR) $(APP_ARGS)

install: libs
	$(MAKE) -C $(LIB_DIR) $(LIB_ARGS) install PREFIX=$(INSTALL_PREFIX)

clean:
	rm -rf build

# Headless Renode run of two boards on one Ethernet switch
# (renode/run-demo.resc) + check of each board's UART1 output.
#   RENODE        Renode executable (required), e.g.
#                 C:/Microchip/SoftConsole-v2022.2-RISC-V-747/renode/Renode.exe
#   RENODE_FLAGS  default: --disable-xwt --console (no windows)
#   SIM_TIME      emulated seconds after the first one, in which the script
#                 injects a device event (default 3)
# Outputs in build/sim: uart1.txt and uart1-board1.txt (MMUART1 of each
# board), renode.log (Renode's console). Both boards must show every line of
# renode/expected-uart.txt; board 0 also those of expected-uart-board0.txt.
# Renode runs in this directory (the script's paths are relative to it) and
# reads end-of-file on stdin, so it exits even if a script command fails.
RENODE_FLAGS ?= --disable-xwt --console
SIM_TIME     ?= 3
SIM_DIR      := build/sim

sim: app
ifeq ($(strip $(RENODE)),)
	$(error RENODE is not set: make sim RENODE=<path to Renode/Renode.exe>, e.g. SoftConsole's renode directory)
endif
	@mkdir -p $(SIM_DIR)
	@rm -f $(SIM_DIR)/uart1.txt $(SIM_DIR)/uart1-board1.txt
	@echo "SIM $(SIM_TIME) s in Renode, log: $(SIM_DIR)/renode.log"
	-"$(RENODE)" $(RENODE_FLAGS) -e '$$uart_log=@$(CURDIR)/$(SIM_DIR)/uart1.txt; $$uart_log_b=@$(CURDIR)/$(SIM_DIR)/uart1-board1.txt; $$runtime="$(SIM_TIME)"; include @renode/run-demo.resc' < /dev/null > $(SIM_DIR)/renode.log 2>&1
	sh renode/check-uart.sh $(SIM_DIR)/uart1.txt $(SIM_DIR)/renode.log renode/expected-uart.txt renode/expected-uart-board0.txt
	sh renode/check-uart.sh $(SIM_DIR)/uart1-board1.txt $(SIM_DIR)/renode.log renode/expected-uart.txt
