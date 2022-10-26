SHELL = /bin/bash

CONTROL_ROOT_DIR := $(abspath $(CURDIR)/..)
include $(CONTROL_ROOT_DIR)/mk/spdk.mk

LIBNAME = server
LIB = libsto_$(LIBNAME).so

CFLAGS = -fPIC -Wall -g -O2 -pthread $(SPDK_CFLAGS)
LDFLAGS = -shared -Wl,-soname,$(LIB) -pthread

C_SRCS = sto_server.c sto_exec.c sto_subprocess_back.c sto_subprocess_rpc.c \
	 sto_aio_back.c sto_aio_rpc.c sto_readdir_back.c sto_readdir_rpc.c
OBJS := ${C_SRCS:.c=.o}

all: $(LIB)

$(LIB): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB)

.PHONY: all clean
