#ifndef _SUBPROCESS_H_
#define _SUBPROCESS_H_

#include <spdk/util.h>

struct sto_subprocess_ctx;

struct sto_subprocess {
	struct sto_subprocess_ctx *subp_ctx;
	const char *file;

	uint64_t timeout;

	pid_t pid;
	TAILQ_ENTRY(sto_subprocess) list;

	bool capture_output;

	void (*release)(struct sto_subprocess *subp, int status);

	int numargs;
	const char *args[];
};

struct sto_subprocess_ctx {
	char output[256];

	int returncode;

	void (*subprocess_done)(struct sto_subprocess_ctx *subp_ctx);
};

int sto_subprocess_init(void);
void sto_subprocess_exit(void);

#define STO_SUBPROCESS(ARGV)	\
	(sto_subprocess_create((ARGV), SPDK_COUNTOF((ARGV)), false, 0))

struct sto_subprocess *
sto_subprocess_create(const char *const argv[], int numargs,
		      bool capture_output, uint64_t timeout);
void sto_subprocess_destroy(struct sto_subprocess *subp);

int sto_subprocess_run(struct sto_subprocess *subp,
		       struct sto_subprocess_ctx *subp_ctx);

#endif /* _SUBPROCESS_H_ */
