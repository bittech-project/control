SHELL = /bin/bash

CONTROL_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(CONTROL_ROOT_DIR)/spdk.mk

PROGRAM = control
SRCS_D = sto_control_app.c sto_exec.c sto_subprocess.c sto_control.c sto_subsystem.c
SRCS_D += rpc/sto_subprocess_rpc.c

SPDK_FLAGS = -pthread -Wl,--whole-archive,-Bstatic \
	$(SPDK_DPDK_LIB) -Wl,--no-whole-archive,-Bdynamic $(SPDK_SYS_LIB)

CONTROL_CFLAGS += -Wall -g -O2 -I../include $(SPDK_CFLAGS)

# Shows how to compile an external application against the SPDK archives.
all:
	$(CC) $(CONTROL_CFLAGS) $(SRCS_D) $(SPDK_FLAGS) -o $(PROGRAM)

clean:
	rm -f $(PROGRAM)

.PHONY: all clean
