# Copyright (C) 2008-2012 Marvell International Ltd.
# All Rights Reserved.
#
# Application Makefile
#
#    Builds and Installs Application ELF Executable Image (.axf)
#    and image suitable for flashing (.bin).
#
#    The Makefile is built using Makefile.targets
#    available in $(TOOLCHAIN_DIR).
#
# Usage:
#
#     Targets:
#
#	  all: Builds the firmware binary image in both ELF (axf) format
#          (suitable for loading into RAM) and bin format (suitable
#          for flashing)
#
#          Should be called with SDK_PATH set to root of a pre-built
#          SDK against which the application should be built.
#
# 		   If BOARD_FILE is set (to the absolute path to the file),
#		   then the file is copied locally (to $(SRC_DIR)/board.c) and
#          then used for build. If its not present, then the application
#          sources should have the needed board-specific functions.
#
#     clean: Cleans all the build artifacts
#
#     install: Installs ELF image, Bin Image, and MAP file to
#              $(INSTALL_DIR). By default INSTALL_DIR = ./bin.
#
# Description:
#
#   Minimally, in this file, only the following need to be specified.
#
#   SRCS     = list of source files
#   DST_NAME = prefix to be used for generated build artifcats.
#
#   Default Variables:
#
#   A number of variables are used in Makefile.targets that can be
#   overridden here.
#
#   SRC_DIR:  directory for source files (default ./src)
#   OBJ_DIR:  directory for intermediate object files (default ./obj)
#   BIN_DIR:  directory for final build artifacts (default ./bin)
#
#   LDSCRIPT: Linker script (default $(TOOLCHAIN_DIR)/mc200.ld)
#   EXTRACFLAGS: pass any additional CFLAGS to be passed to the C Compiler.

include config.mk


LDSCRIPT = $(TOOLCHAIN_DIR)/mc200.ld

ifeq ($(APPCONFIG_OVERLAY_ENABLE),y)
	LDSCRIPT = ./src/wm_demo_overlay.ld
	SRCS-OPT += overlays.c 	wm_demo_overlays.c
	EXTRACFLAGS += -DAPPCONFIG_OVERLAY_ENABLE
endif

ifeq ($(APPCONFIG_PROV_EZCONNECT),y)
	EXTRACFLAGS += -DAPPCONFIG_PROV_EZCONNECT
endif

DST_NAME = wm_demo

SRCS = main.c \
	reset_prov_helper.c \
	led_indicator.c \
	wmcloud.c \
	wmcloud_helper.c \
	wm_demo_wps_cli.c \
	$(SRCS-OPT) board.c

ifeq (y,$(LONG_POLL_CLOUD))
SRCS += wmcloud_lp.c wm_demo_cloud.c
endif

ifeq (y,$(WEBSOCKET_CLOUD))
SRCS += wmcloud_ws.c wm_demo_cloud.c
endif

ifeq (y,$(XIVELY_CLOUD))
SRCS += wmcloud_xively.c wm_demo_xively_cloud.c
endif

ifeq (y,$(ARRAYENT_CLOUD))
SRCS += wmcloud_arrayent.c wm_demo_arrayent_cloud.c
endif

EXTRACFLAGS-$(APPCONFIG_HTTPS_CLOUD) += -DAPPCONFIG_HTTPS_CLOUD

SRCS-$(APPCONFIG_PM_ENABLE) += power_mgr_helper.c
EXTRACFLAGS-$(APPCONFIG_PM_ENABLE) += -DAPPCONFIG_PM_ENABLE

SRCS-$(APPCONFIG_MDNS_ENABLE) += mdns_helper.c
EXTRACFLAGS-$(APPCONFIG_MDNS_ENABLE) += -DAPPCONFIG_MDNS_ENABLE

SRCS-$(APPCONFIG_WPS_ENABLE) += wps_helper.c
EXTRACFLAGS-$(APPCONFIG_WPS_ENABLE) += -DAPPCONFIG_WPS_ENABLE


EXTRACFLAGS += -I./src -D APPCONFIG_DEBUG_ENABLE=1 -D APPCONFIG_DEMO_CLOUD=1 $(EXTRACFLAGS-y)

SRCS += $(SRCS-y)

MAKE_FTFS = 1
FTFS_DIR  = ./www
FTFS_API_VERSION = 100

include $(TOOLCHAIN_DIR)/targets.mk
include $(TOOLCHAIN_DIR)/rules.mk

