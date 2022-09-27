#ifndef _STO_EXEC_H_
#define _STO_EXEC_H_

#include <pthread.h>

struct sto_exec_ops {
	const char *name;

	int (*exec)(void *arg);
	void (*exec_done)(void *arg, int rc);
};

struct sto_exec_ctx {
	pthread_t thread;

	struct sto_exec_ops *ops;
	void *priv;
};

void sto_exec_init_ctx(struct sto_exec_ctx *exec_ctx, struct sto_exec_ops *ops, void *priv);

int sto_exec(struct sto_exec_ctx *exec_ctx);

#endif /* _STO_EXEC_H_ */
