#ifndef _STO_SRV_SUBPROCESS_H_
#define _STO_SRV_SUBPROCESS_H_

#include <spdk/util.h>

#include "sto_exec.h"

struct sto_subprocess_back;
typedef void (*subprocess_back_done_t)(struct sto_subprocess_back *subp);

struct sto_subprocess_back {
	struct sto_exec_ctx exec_ctx;

	bool capture_output;

	int returncode;
	char output[256];
	size_t output_sz;

	int pipefd[2];

	void *priv;
	subprocess_back_done_t subprocess_back_done;

	int numargs;
	const char *file;
	const char *args[];
};

#define STO_SUBPROCESS_BACK(ARGV)	\
	(sto_subprocess_back_alloc((ARGV), SPDK_COUNTOF((ARGV)), false))

struct sto_subprocess_back *sto_subprocess_back_alloc(const char *const argv[],
		int numargs, bool capture_output);
void sto_subprocess_back_free(struct sto_subprocess_back *subp);
void sto_subprocess_back_init_cb(struct sto_subprocess_back *subp,
				 subprocess_back_done_t subprocess_back_done, void *priv);

int sto_subprocess_back_run(struct sto_subprocess_back *subp);

#endif /* _STO_SRV_SUBPROCESS_H_ */
