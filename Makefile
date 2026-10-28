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
#   make sim-host RENODE=<renode>  the same plus the host on the simulated
#                             network (renode/run-host.resc, host_bridge.py;
#                             needs Python 3)
#   make robot RENODE_TEST=<renode-test>  the Robot Framework tests
#                             (renode/tests/mpfs-demo.robot)
#
# Variables given here (BUILD_TYPE, CROSS_COMPILE, ...) are passed on to both
# Makefiles; see src/mk/toolchain.mk.

LIB_DIR := src
APP_DIR := examples/freertos-demo

# The demo's FreeRTOSConfig.h and FreeRTOSIPConfig.h are compiled into the
# libraries (relative to src/).
LIB_ARGS := FREERTOS_CONFIG_DIR=../$(APP_DIR) FREERTOS_IP_CONFIG_DIR=../$(APP_DIR)
APP_ARGS := MPFS_SMODE_DIR=../../build/dist OUT_DIR=../../build/$(APP_DIR)

# PREFIX as given here, made usable from src/ (absolute paths kept).
INSTALL_PREFIX = $(if $(filter /% %:%,$(subst \,/,$(PREFIX))),$(PREFIX),../$(PREFIX))

.PHONY: all libs app install clean sim sim-host robot

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

# The same two boards plus the host: renode/host_bridge.py (Python 3) runs as
# 192.168.10.1 behind the SocketBridge model (localhost UDP 37000/37001, no
# TAP, no administrator rights), pings both boards and answers their pings.
#   PYTHON        Python 3 interpreter (default: python3; on Windows e.g.
#                 PYTHON=py or the full path to python.exe)
# Extra outputs in build/sim: host.txt (the helper's events, checked against
# renode/expected-host.txt), host-bridge.log.
PYTHON ?= python3

sim-host: app
ifeq ($(strip $(RENODE)),)
	$(error RENODE is not set: make sim-host RENODE=<path to Renode/Renode.exe>)
endif
	@mkdir -p $(SIM_DIR)
	@rm -f $(SIM_DIR)/uart1.txt $(SIM_DIR)/uart1-board1.txt $(SIM_DIR)/host.txt
	@echo "SIM $(SIM_TIME) s in Renode with the host bridge, log: $(SIM_DIR)/renode.log"
	-"$(PYTHON)" renode/host_bridge.py --ping 192.168.10.10 --ping 192.168.10.11 \
	    --duration 600 --report $(SIM_DIR)/host.txt > $(SIM_DIR)/host-bridge.log 2>&1 & \
	  helper=$$!; \
	  "$(RENODE)" $(RENODE_FLAGS) -e '$$uart_log=@$(CURDIR)/$(SIM_DIR)/uart1.txt; $$uart_log_b=@$(CURDIR)/$(SIM_DIR)/uart1-board1.txt; $$runtime="$(SIM_TIME)"; include @renode/run-host.resc' < /dev/null > $(SIM_DIR)/renode.log 2>&1; \
	  kill $$helper 2>/dev/null; wait $$helper 2>/dev/null; true
	sh renode/check-uart.sh $(SIM_DIR)/uart1.txt $(SIM_DIR)/renode.log renode/expected-uart.txt renode/expected-uart-board0.txt renode/expected-host-board.txt
	sh renode/check-uart.sh $(SIM_DIR)/uart1-board1.txt $(SIM_DIR)/renode.log renode/expected-uart.txt renode/expected-host-board.txt
	sh renode/check-uart.sh $(SIM_DIR)/host.txt $(SIM_DIR)/host-bridge.log renode/expected-host.txt

# Robot Framework tests (renode/tests/mpfs-demo.robot): the same two boards,
# checked with terminal testers. renode-test comes with Renode and needs
# Python 3 with Renode's tests/requirements.txt (robotframework==4.0.1, ...).
#   RENODE_TEST   the renode-test script (required)
# Results (report.html, log.html) in build/robot.
robot: app
ifeq ($(strip $(RENODE_TEST)),)
	$(error RENODE_TEST is not set: make robot RENODE_TEST=<path to Renode's renode-test>)
endif
	@mkdir -p build/robot
	"$(RENODE_TEST)" renode/tests/mpfs-demo.robot -r build/robot
