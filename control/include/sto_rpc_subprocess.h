#ifndef _STO_RPC_SUBPROCESS_H_
#define _STO_RPC_SUBPROCESS_H_

#include <spdk/util.h>

struct sto_subprocess;
typedef void (*subprocess_done_t)(struct sto_subprocess *subp);

struct sto_subprocess {
	struct {
		int returncode;
		char *output;
	}; /* result related fields */

	bool capture_output;

	void *priv;
	subprocess_done_t subprocess_done;

	int numargs;
	const char *args[];
};

struct sto_subprocess *sto_subprocess_alloc(const char *const argv[],
					    int numargs, bool capture_output);
void sto_subprocess_init_cb(struct sto_subprocess *subp,
			    subprocess_done_t subprocess_done, void *priv);
void sto_subprocess_free(struct sto_subprocess *subp);

int sto_subprocess_run(struct sto_subprocess *subp);

#define STO_SUBPROCESS_EXEC(cmd, cmd_done, priv) \
	sto_subprocess_exec(cmd, SPDK_COUNTOF(cmd), cmd_done, priv)
int sto_subprocess_exec(const char *const cmd[], int numargs,
			subprocess_done_t cmd_done, void *priv);

#endif /* _STO_RPC_SUBPROCESS_H_ */
