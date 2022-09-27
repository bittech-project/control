SHELL = /bin/bash

CONTROL_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(CONTROL_ROOT_DIR)/mk/spdk.mk

CFLAGS = -Wall -g -O2 -pthread -I../include $(SPDK_CFLAGS)
LDFLAGS = -pthread $(SPDK_LDFLAGS) -Wl,--whole-archive,-Bstatic \
	  $(SPDK_DPDK_LIB) -Wl,--no-whole-archive,-Bdynamic $(SPDK_SYS_LIB)

PROG = control

C_SRCS = main.c sto_exec.c sto_subprocess.c sto_control.c sto_subsystem.c \
	 rpc/sto_exec_rpc.c \
	 sto_client.c \
	 server/sto_server.c server/sto_subprocess_rpc.c
OBJS := ${C_SRCS:.c=.o}

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PROG)

.PHONY: all clean
