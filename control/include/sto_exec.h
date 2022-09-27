#ifndef _STO_EXEC_H_
#define _STO_EXEC_H_

#include <spdk/queue.h>

struct sto_exec_ops {
	const char *name;

	int (*pre_fork)(void *arg);
	int (*exec)(void *arg);
	int (*post_fork)(void *arg, pid_t pid);
	void (*exec_done)(void *arg);
};

struct sto_exec_ctx {
	struct sto_exec_ops *ops;
	void *priv;

	struct {
		int exitval;
		int signal;
		bool exited;
		pid_t pid;
	}; /* child process related members */

	TAILQ_ENTRY(sto_exec_ctx) list;
};

int sto_exec_init(void);
void sto_exec_exit(void);

void sto_exec_init_ctx(struct sto_exec_ctx *exec_ctx, struct sto_exec_ops *ops, void *priv);

int sto_exec(struct sto_exec_ctx *exec_ctx);

static inline int
sto_exec_get_result(struct sto_exec_ctx *exec_ctx)
{
	return exec_ctx->exitval;
}

#endif /* _STO_EXEC_H_ */
