#ifndef _STO_SUBPROCESS_H_
#define _STO_SUBPROCESS_H_

#include <spdk/util.h>

#include "sto_exec.h"

struct sto_subprocess_ctx;
typedef void (subprocess_done_t)(struct sto_subprocess_ctx *subp_ctx);

struct sto_subprocess {
	struct sto_exec_ctx exec_ctx;
	struct sto_subprocess_ctx *subp_ctx;
	const char *file;

	bool capture_output;
	int pipefd[2];

	int numargs;
	const char *args[];
};

struct sto_subprocess_ctx {
	char output[256];
	int returncode;

	void *priv;
	subprocess_done_t *subprocess_done;
};

#define STO_SUBPROCESS(ARGV)	\
	(sto_subprocess_create((ARGV), SPDK_COUNTOF((ARGV)), false))

struct sto_subprocess *
sto_subprocess_create(const char *const argv[], int numargs, bool capture_output);
void sto_subprocess_init_cb(struct sto_subprocess_ctx *subp_ctx,
			    subprocess_done_t *subprocess_done, void *priv);
void sto_subprocess_destroy(struct sto_subprocess *subp);

int sto_subprocess_run(struct sto_subprocess *subp,
		       struct sto_subprocess_ctx *subp_ctx);

#endif /* _STO_SUBPROCESS_H_ */
