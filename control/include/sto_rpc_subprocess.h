#ifndef _STO_RPC_SUBPROCESS_H_
#define _STO_RPC_SUBPROCESS_H_

#include <spdk/util.h>

struct sto_rpc_subprocess_cmd;
typedef void (*sto_rpc_subprocess_done_t)(struct sto_rpc_subprocess_cmd *cmd);

struct sto_rpc_subprocess_cmd {
	struct {
		int returncode;
		char *output;
	}; /* result related fields */

	bool capture_output;

	void *priv;
	sto_rpc_subprocess_done_t done;

	int numargs;
	const char *args[];
};

struct sto_rpc_subprocess_cmd *sto_rpc_subprocess_cmd_alloc(const char *const argv[],
							    int numargs, bool capture_output);
void sto_rpc_subprocess_cmd_init_cb(struct sto_rpc_subprocess_cmd *cmd,
				    sto_rpc_subprocess_done_t done, void *priv);
void sto_rpc_subprocess_cmd_free(struct sto_rpc_subprocess_cmd *cmd);

int sto_rpc_subprocess_cmd_run(struct sto_rpc_subprocess_cmd *cmd);

#define STO_RPC_SUBPROCESS(cmd, done, priv) \
	sto_rpc_subprocess(cmd, SPDK_COUNTOF(cmd), done, priv)
int sto_rpc_subprocess(const char *const cmd[], int numargs,
		       sto_rpc_subprocess_done_t done, void *priv);

#endif /* _STO_RPC_SUBPROCESS_H_ */
