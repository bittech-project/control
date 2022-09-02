#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (c) Intel Corporation.
#  All rights reserved.
#

include $(CURDIR)/../config.mk

# Shows how to compile an external application against the SPDK archives.
all:
	$(CC) $(COMMON_CFLAGS) -o dummy ./main.c -pthread -Wl,--whole-archive,-Bstatic \
 	$(SPDK_DPDK_LIB) -Wl,--no-whole-archive,-Bdynamic $(SYS_LIB)

clean:
	rm -f dummy

.PHONY: all clean
