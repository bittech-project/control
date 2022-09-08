CONTROL_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(CONTROL_ROOT_DIR)/spdk.mk

PROGRAM = proc
SRCS_D = proc.c sto_subprocess.c

SPDK_FLAGS = -pthread -Wl,--whole-archive,-Bstatic \
	$(SPDK_DPDK_LIB) -Wl,--no-whole-archive,-Bdynamic $(SPDK_SYS_LIB)

COMMON_CFLAGS+=-I../include $(SPDK_CFLAGS)

# Shows how to compile an external application against the SPDK archives.
all:
	$(CC) $(COMMON_CFLAGS) $(SRCS_D) $(SPDK_FLAGS) -o $(PROGRAM)

clean:
	rm -f $(PROGRAM)

.PHONY: all clean
