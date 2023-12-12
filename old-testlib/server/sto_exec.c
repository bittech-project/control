#include "sto_exec.h"

#include <spdk/stdinc.h>
#include <spdk/likely.h>

void
sto_exec_init_ctx(struct sto_exec_ctx *exec_ctx, struct sto_exec_ops *ops, void *priv)
{
	exec_ctx->ops = ops;
	exec_ctx->priv = priv;
}

static void *
sto_local_exec(void *arg)
{
	struct sto_exec_ctx *exec_ctx = arg;
	struct sto_exec_ops *ops;
	int rc;

	ops = exec_ctx->ops;

	rc = ops->exec(exec_ctx->priv);
	if (spdk_unlikely(rc)) {
		printf("%s->exec() failed, rc=%d\n", ops->name, rc);
	}

	ops->exec_done(exec_ctx->priv, rc);

	return (void *) 0;
}

int
sto_exec(struct sto_exec_ctx *exec_ctx)
{
	pthread_attr_t attr;
	int rc = 0;

	rc = pthread_attr_init(&attr);
	if (spdk_unlikely(rc)) {
		printf("pthread_attr_init() failed, rc=%d\n", rc);
		return rc;
	}

	rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (spdk_unlikely(rc)) {
		printf("Failed to set detach state to the thread, rc=%d\n", rc);
		return rc;
	}

	rc = pthread_create(&exec_ctx->thread, &attr, sto_local_exec, exec_ctx);
	if (spdk_unlikely(rc)) {
		printf("Failed to create processing exec thread, rc=%d\n", rc);
	}

	return rc;
}
