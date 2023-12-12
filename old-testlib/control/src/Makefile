SHELL = /bin/bash

CONTROL_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(CONTROL_ROOT_DIR)/mk/sto.common.mk
include $(CONTROL_ROOT_DIR)/mk/spdk.mk
include $(CONTROL_ROOT_DIR)/mk/sto.server.mk

PROG = control

CFLAGS = -O2 -pthread -I../include $(STO_SERVER_CFLAGS)
LDFLAGS = -pthread $(SPDK_LDFLAGS) -Wl,--whole-archive,-Bstatic \
	  $(SPDK_DPDK_LIB) -Wl,--no-whole-archive,-Bdynamic $(SPDK_SYS_LIB) \
	  $(STO_SERVER_LDFLAGS) -Wl,--no-whole-archive,-Bdynamic $(STO_SERVER_LIB)

C_SRCS = main.c sto_control_rpc.c sto_client.c sto_core.c \
	 sto_component.c sto_subsystem.c sto_module.c \
	 lib/sto_lib.c lib/sto_req.c lib/sto_pipeline.c lib/sto_generic_req.c lib/util/sto_json.c lib/sto_inode.c lib/sto_tree.c lib/sto_hash.c \
	 server_rpc/sto_rpc_subprocess.c server_rpc/sto_rpc_aio.c server_rpc/sto_rpc_readdir.c \
	 subsystems/scst/scst_subsystem.c subsystems/scst/scst_lib.c subsystems/scst/scst_main.c subsystems/scst/scst_config.c \
	 subsystems/sys/sys_lib.c \
	 modules/config/config_mod.c modules/scst/scst_mod.c
OBJS := ${C_SRCS:.c=.o}

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(COMMON_CFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PROG)

.PHONY: all clean
