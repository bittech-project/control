#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (c) Intel Corporation.
#  All rights reserved.
#

include $(CURDIR)/../config.mk

PROGRAM = dummy
SRCS_D = main.c

SPDK_FLAGS = -pthread -Wl,--whole-archive,-Bstatic \
	$(SPDK_DPDK_LIB) -Wl,--no-whole-archive,-Bdynamic $(SPDK_SYS_LIB)

# Shows how to compile an external application against the SPDK archives.
all:
	$(CC) $(SPDK_CFLAGS) $(SRCS_D) $(SPDK_FLAGS) -o $(PROGRAM)

clean:
	rm -f $(PROGRAM)

.PHONY: all clean
