#include <spdk/thread.h>
#include <spdk/log.h>
#include <spdk/likely.h>

#include "sto_exec.h"

#define STO_EXEC_POLL_PERIOD	4000 /* 4ms */

static struct spdk_poller *sto_exec_poller;

TAILQ_HEAD(sto_exec_list, sto_exec_ctx);

static struct sto_exec_list sto_exec_list =
	TAILQ_HEAD_INITIALIZER(sto_exec_list);

static int sto_exec_initialized;

static void
sto_exec_check_child(struct sto_exec_ctx *exec_ctx)
{
	int ret, status;

	ret = waitpid(exec_ctx->pid, &status, WNOHANG);
	if (ret < 0) {
		if (errno == ECHILD) {
			SPDK_ERRLOG("connection pid %u disappeared\n",
				    (int) exec_ctx->pid);
			exec_ctx->exited = true;
		} else {
			SPDK_ERRLOG("waitpid: %s\n", strerror(errno));
		}
	} else if (ret == exec_ctx->pid) {
		if (WIFSIGNALED(status)) {
			exec_ctx->signal = WTERMSIG(status);
			exec_ctx->exited = true;
		}

		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status)) {
				exec_ctx->exitval = WEXITSTATUS(status);
			}

			exec_ctx->exited = true;
		}
	}
}

static int
sto_exec_poll(void *arg)
{
	struct sto_exec_ctx *exec_ctx, *tmp;

	TAILQ_FOREACH_SAFE(exec_ctx, &sto_exec_list, list, tmp) {
		sto_exec_check_child(exec_ctx);

		if (exec_ctx->exited) {
			/* child is finished */
			TAILQ_REMOVE(&sto_exec_list, exec_ctx, list);
			exec_ctx->ops->exec_done(exec_ctx->priv);
		}
	}
}

int
sto_exec_init(void)
{
	if (spdk_unlikely(sto_exec_initialized)) {
		SPDK_ERRLOG("STO exec lib has already been initialized\n");
		return -EINVAL;
	}

	sto_exec_poller = SPDK_POLLER_REGISTER(sto_exec_poll, NULL, STO_EXEC_POLL_PERIOD);
	if (spdk_unlikely(!sto_exec_poller)) {
		SPDK_ERRLOG("Cann't register the STO exec poller\n");
		return -EFAULT;
	}

	sto_exec_initialized = 1;

	return 0;
}

void
sto_exec_exit(void)
{
	if (spdk_unlikely(!sto_exec_initialized)) {
		SPDK_ERRLOG("STO exec lib hasn't been initialized yet\n");
		return;
	}

	spdk_poller_unregister(&sto_exec_poller);
	sto_exec_initialized = 0;
}

void
sto_exec_init_ctx(struct sto_exec_ctx *exec_ctx, struct sto_exec_ops *ops, void *priv)
{
	exec_ctx->ops = ops;
	exec_ctx->priv = priv;
}

int
sto_exec(struct sto_exec_ctx *exec_ctx)
{
	pid_t pid;
	int rc = 0;

	if (spdk_unlikely(!sto_exec_initialized)) {
		SPDK_ERRLOG("STO subprocess lib hasn't been initialized yet\n");
		return -EINVAL;
	}

	if (exec_ctx->ops->pre_fork) {
		rc = exec_ctx->ops->pre_fork(exec_ctx->priv);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("%s->pre_fork() failed, rc=%d\n",
				    exec_ctx->ops->name, rc);
			return rc;
		}
	}

	pid = fork();
	if (spdk_unlikely(pid == -1)) {
		SPDK_ERRLOG("Failed to fork: %s\n", strerror(errno));
		return -errno;
	}

	if (exec_ctx->ops->post_fork) {
		rc = exec_ctx->ops->post_fork(exec_ctx->priv, pid);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("%s->post_fork() failed, rc=%d\n",
				    exec_ctx->ops->name, rc);
		}
	}

	/* Child */
	if (!pid) {
		rc = exec_ctx->ops->exec(exec_ctx->priv);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("%s->exec() failed, rc=%d\n", exec_ctx->ops->name, rc);
		}

		exit(rc);
	}

	/* Parent */
	exec_ctx->pid = pid;
	TAILQ_INSERT_TAIL(&sto_exec_list, exec_ctx, list);

	return 0;
}
