SHELL = /bin/bash

CONTROL_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(CONTROL_ROOT_DIR)/mk/spdk.mk
include $(CONTROL_ROOT_DIR)/mk/sto.server.mk

PROG = control

SCST_SUBSYS = subsystems/scst

CFLAGS = -Wall -g -O2 -pthread -I../include $(STO_SERVER_CFLAGS) $(SPDK_CFLAGS)
LDFLAGS = -pthread $(SPDK_LDFLAGS) -Wl,--whole-archive,-Bstatic \
	  $(SPDK_DPDK_LIB) -Wl,--no-whole-archive,-Bdynamic $(SPDK_SYS_LIB) \
	  $(STO_SERVER_LDFLAGS) -Wl,--no-whole-archive,-Bdynamic $(STO_SERVER_LIB)

C_SRCS = main.c sto_spdk_subsystem.c sto_control.c sto_subsystem.c sto_lib.c sto_core.c \
	 client/sto_client.c client/sto_subprocess_front.c client/sto_aio_front.c client/sto_readdir_front.c \
	 rpc/sto_exec_rpc.c rpc/sto_aio_rpc.c rpc/sto_control_rpc.c rpc/sto_readdir_rpc.c \
	 $(SCST_SUBSYS)/scst_main.c $(SCST_SUBSYS)/scst_subsystem.c $(SCST_SUBSYS)/scst_lib.c
OBJS := ${C_SRCS:.c=.o}

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PROG)

.PHONY: all clean
