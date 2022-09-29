#ifndef _STO_SUBPROCESS_H_
#define _STO_SUBPROCESS_H_

#include <spdk/util.h>

#include "sto_exec.h"

struct sto_subprocess;
typedef void (*subprocess_done_t)(struct sto_subprocess *subp);

struct sto_subprocess {
	struct sto_exec_ctx exec_ctx;

	bool capture_output;
	char output[256];
	size_t output_sz;

	int pipefd[2];
	int returncode;

	void *priv;
	subprocess_done_t subprocess_done;

	int numargs;
	const char *file;
	const char *args[];
};

#define STO_SUBPROCESS(ARGV)	\
	(sto_subprocess_alloc((ARGV), SPDK_COUNTOF((ARGV)), false))

struct sto_subprocess *
sto_subprocess_alloc(const char *const argv[], int numargs, bool capture_output);
void sto_subprocess_free(struct sto_subprocess *subp);
void sto_subprocess_init_cb(struct sto_subprocess *subp,
			    subprocess_done_t subprocess_done, void *priv);

int sto_subprocess_run(struct sto_subprocess *subp);

#endif /* _STO_SUBPROCESS_H_ */
