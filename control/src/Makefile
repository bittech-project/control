SHELL = /bin/bash

CONTROL_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(CONTROL_ROOT_DIR)/mk/spdk.mk

CFLAGS = -Wall -g -O2 -pthread -I../include $(SPDK_CFLAGS)
LDFLAGS = -pthread $(SPDK_LDFLAGS) -Wl,--whole-archive,-Bstatic \
	  $(SPDK_DPDK_LIB) -Wl,--no-whole-archive,-Bdynamic $(SPDK_SYS_LIB)

PROG = control

C_SRCS = main.c sto_spdk_subsystem.c sto_control.c sto_subsystem.c sto_core.c \
	 subsystems/scst/sto_scst.c \
	 client/sto_client.c client/sto_subprocess_front.c \
	 server/sto_server.c server/sto_exec.c server/sto_subprocess_back.c server/sto_subprocess_rpc.c \
	 server/sto_aio.c server/sto_aio_rpc.c \
	 rpc/sto_exec_rpc.c rpc/sto_aio_rpc.c rpc/sto_scst_rpc.c rpc/sto_core_rpc.c
OBJS := ${C_SRCS:.c=.o}

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PROG)

.PHONY: all clean
