# SPDX-License-Identifier: GPL-2.0
#
# Makefile for bigwave
#

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

EXTRA_CFLAGS	+= -DDYNAMIC_DEBUG_MODULE

include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include

EXTRA_CFLAGS += -I$(ROOT_DIR)/private/google-modules/video/gchips/include
EXTRA_CFLAGS += -DENABLE_SLC=1

modules modules_install headers_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)

modules_install: headers_install
