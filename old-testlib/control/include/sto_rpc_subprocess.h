#ifndef _STO_RPC_SUBPROCESS_H_
#define _STO_RPC_SUBPROCESS_H_

#include <spdk/util.h>

#include "sto_async.h"

struct sto_rpc_subprocess_args {
	void *cb_arg;
	sto_generic_cb cb_fn;

	char **output;
};

void sto_rpc_subprocess(const char *const *argv, sto_generic_cb cb_fn, void *cb_arg, char **output);
void sto_rpc_subprocess_fmt(const char *fmt, sto_generic_cb cb_fn, void *cb_arg, char **output, ...);

#endif /* _STO_RPC_SUBPROCESS_H_ */
