# Copyright (C) 2008-2013 Marvell International Ltd.
# All Rights Reserved.

include ../config_cmd

ifneq ($(NOISY),1)
AT=@
SILENT=-s
endif

# Location of GNU toolchain specific files
#TOOLCHAIN_DIR := $(CURDIR)/toolchains/gnu

# Install in ./bin if no INSTALL_DIR is passed
INSTALL_DIR   ?= $(CURDIR)/bin

SDK_PATH = $(SDK_DIR)

export TOOLCHAIN_DIR INSTALL_DIR
export SDK_PATH BOARD_FILE

-include $(SDK_PATH)/.config

# All the apps to build
APPS = wlan/wm_demo	


# Install only a select few apps
INSTALL_APPS = $(APPS)

all: apps install

app_build := $(subst /,@,$(APPS))
apps:  $(app_build)

$(app_build): check_precond
	M=$(subst @,/,$@); \
	$(MAKE) $(SILENT) -C $(CURDIR)/$$M all;\
	if [ $$? != 0 ]; then exit 1; fi; \

.PHONY: board_file

board_file:
	@for M in $(APPS); do \
		echo $$M; \
		$(MAKE) $(SILENT) -C $(CURDIR)/$$M board_file;\
		if [ $$? != 0 ]; then exit 1; fi; \
	done;


clean:
	@for M in $(APPS); do \
		$(MAKE) $(SILENT) -C $(CURDIR)/$$M clean; \
	done;

install:
	@for M in $(INSTALL_APPS); do \
	cp -f $(CURDIR)/$$M/bin/*.bin ../bin	\
#		$(MAKE) $(SILENT) -C $(CURDIR)/$$M install; \
	done;	

# Convenience Checking rules
#
# check_perm:
#  -- Ensure that the tarball is extracted with the right permissions
# check_sdk:
#   -- Ensure that SDK_PATH variable is set and points to a pre-built
#      SDK
#

check_precond: check_perm check_sdk

check_perm:
	@PERM=`ls -l Makefile | awk '{print $$1}'`; \
	if [  "$$PERM" =  "----------+" ]; then \
	echo "ERROR: File permissions do not seem right."; \
	echo "Please use the tar cmd line program to untar the software tarballs!";	\
	exit 1; \
	fi

check_sdk:
#   Ensure that SDK_PATH variable is set.
ifeq ($(SDK_PATH),)
	$(error "ERROR: SDK_PATH not set. Please set SDK_PATH variable to a configured and compiled SDK")
endif
	@if [ ! -e $(SDK_PATH)/.config ] || [ ! -e $(SDK_PATH)/incl/autoconf.h ]; then \
		echo "ERROR: $(SDK_PATH) does not contain configured and compiled SDK"; \
		exit 1; \
	fi
