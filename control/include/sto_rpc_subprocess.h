#ifndef _STO_RPC_SUBPROCESS_H_
#define _STO_RPC_SUBPROCESS_H_

#include <spdk/util.h>

typedef void (*sto_rpc_subprocess_done_t)(void *priv, int rc);

struct sto_rpc_subprocess_args {
	void *priv;
	sto_rpc_subprocess_done_t done;

	char **output;
};

int sto_rpc_subprocess(const char *const *argv, struct sto_rpc_subprocess_args *args);
int sto_rpc_subprocess_fmt(const char *fmt, struct sto_rpc_subprocess_args *args, ...);

#endif /* _STO_RPC_SUBPROCESS_H_ */
