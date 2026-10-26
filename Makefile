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

.PHONY: all libs app install clean

all: app

libs:
	$(MAKE) -C $(LIB_DIR) $(LIB_ARGS)

app: libs
	$(MAKE) -C $(APP_DIR) $(APP_ARGS)

install: libs
	$(MAKE) -C $(LIB_DIR) $(LIB_ARGS) install PREFIX=$(INSTALL_PREFIX)

clean:
	rm -rf build
