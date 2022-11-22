SHELL = /bin/bash

CONTROL_ROOT_DIR := $(abspath $(CURDIR)/..)
include $(CONTROL_ROOT_DIR)/mk/spdk.mk

LIBNAME = server
LIB = libsto_$(LIBNAME).so

CFLAGS = -fPIC -Wall -g -O2 -pthread -I./include $(SPDK_CFLAGS)
LDFLAGS = -shared -Wl,-soname,$(LIB) -pthread

C_SRCS = sto_server.c sto_exec.c sto_srv_rpc.c sto_subprocess_back.c sto_subprocess_rpc.c \
	 fs/sto_srv_fs.c fs/sto_srv_aio.c fs/sto_srv_readdir.c
OBJS := ${C_SRCS:.c=.o}

all: $(LIB)

$(LIB): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB)

.PHONY: all clean
