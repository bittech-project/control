#include <spdk/stdinc.h>

#include <spdk/log.h>
#include <spdk/likely.h>

#include <rte_malloc.h>

#include "sto_subprocess.h"
#include "sto_exec.h"

static int sto_subprocess_pre_fork(void *arg);
static int sto_subprocess_exec(void *arg);
static int sto_subprocess_post_fork(void *arg, pid_t pid);
static void sto_subprocess_exec_done(void *arg);

static struct sto_exec_ops subprocess_ops = {
	.name = "subprocess",
	.pre_fork = sto_subprocess_pre_fork,
	.exec = sto_subprocess_exec,
	.post_fork = sto_subprocess_post_fork,
	.exec_done = sto_subprocess_exec_done,
};

static int
sto_redirect_to_null(void)
{
	int fd, rc;

	fd = open("/dev/null", O_WRONLY);
	if (spdk_unlikely(fd == -1)) {
		SPDK_ERRLOG("Failed to open /dev/null: %s\n",
			    strerror(errno));
		return -errno;
	}

	rc = dup2(fd, 1);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to dup2 stdout: %s\n",
			    strerror(errno));
		return -errno;
	}

	rc = dup2(fd, 2);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to dup2 stderr: %s\n",
			    strerror(errno));
		return -errno;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to close /dev/null: %s",
			    strerror(errno));
		return -errno;
	}

	return 0;
}

static int
sto_setup_pipe(int pipefd[2], int dir)
{
	int rc;

	/* close read/write end of pipe */
	rc = close(pipefd[!dir]);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to child close (pipefd[%d]): %s",
			    !dir, strerror(errno));
		return -errno;
	}

	/* make 0/1 same as read/write-to end of pipe */
	rc = dup2(pipefd[dir], dir);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to child dup2 (pipefd[%d]): %s",
			    dir, strerror(errno));
		return -errno;
	}

	/* close excess fildes */
	rc = close(pipefd[dir]);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to child close (pipefd[%d]): %s",
			    dir, strerror(errno));
		return -errno;
	}

	return 0;
}

static int
sto_subprocess_pre_fork(void *arg)
{
	struct sto_subprocess *subp = arg;

	if (subp->capture_output) {
		int rc = pipe(subp->pipefd);
		if (spdk_unlikely(rc == -1)) {
			SPDK_ERRLOG("Failed to create subprocess pipe: %s\n",
				    strerror(errno));
			return -errno;
		}
	}

	return 0;
}

static int
sto_subprocess_exec(void *arg)
{
	struct sto_subprocess *subp = arg;
	int rc = 0;

	if (subp->capture_output) {
		rc = sto_setup_pipe(subp->pipefd, STDOUT_FILENO);
	} else {
		sto_redirect_to_null();
	}

	/*
	 * execvp() takes (char *const *) for backward compatibility,
	 * but POSIX guarantees that it will not modify the strings,
	 * so the cast is safe
	 */
	rc = execvp(subp->file, (char *const *) subp->args);
	if (rc == -1) {
		SPDK_ERRLOG("Failed to execvp: %s", strerror(errno));
	}

	return -errno;
}

static int
sto_subprocess_post_fork(void *arg, pid_t pid)
{
	struct sto_subprocess *subp = arg;
	int rc = 0;

	/* Parent */
	if (pid && subp->capture_output) {
		rc = sto_setup_pipe(subp->pipefd, STDIN_FILENO);
	}

	return rc;
}

static void
sto_subprocess_exec_done(void *arg)
{
	struct sto_subprocess *subp = arg;
	struct sto_subprocess_ctx *subp_ctx = subp->subp_ctx;

	subp_ctx->returncode = subp->exec_ctx.exitval;

	if (subp->capture_output) {
		ssize_t read_sz;

		memset(subp_ctx->output, 0, sizeof(subp_ctx->output));

		read_sz = read(0, subp_ctx->output, sizeof(subp_ctx->output) - 1);
	}

	sto_subprocess_destroy(subp);

	subp_ctx->subprocess_done(subp_ctx);
}

struct sto_subprocess *
sto_subprocess_create(const char *const argv[], int numargs, bool capture_output)
{
	struct sto_subprocess *subp;
	unsigned int data_len;
	int i;

	if (spdk_unlikely(!numargs)) {
		SPDK_ERRLOG("Too few arguments\n");
		return NULL;
	}

	/* Count the number of bytes for the 'numargs' arguments to be allocated */
	data_len = numargs * sizeof(char *);

	subp = rte_zmalloc(NULL, sizeof(*subp) + data_len, 0);
	if (spdk_unlikely(!subp)) {
		SPDK_ERRLOG("Cann't allocate memory for subprocess\n");
		return NULL;
	}

	sto_exec_init_ctx(&subp->exec_ctx, &subprocess_ops, subp);

	subp->capture_output = capture_output;
	subp->numargs = numargs;

	subp->file = argv[0];

	for (i = 0; i < numargs; i++) {
		subp->args[i] = argv[i];
	}

	return subp;
}

void
sto_subprocess_init_cb(struct sto_subprocess_ctx *subp_ctx,
		       subprocess_done_t *subprocess_done, void *priv)
{
	subp_ctx->subprocess_done = subprocess_done;
	subp_ctx->priv = priv;
}

void
sto_subprocess_destroy(struct sto_subprocess *subp)
{
	rte_free(subp);
}

int
sto_subprocess_run(struct sto_subprocess *subp,
		   struct sto_subprocess_ctx *subp_ctx)
{
	int rc = 0;

	subp->subp_ctx = subp_ctx;

	rc = sto_exec(&subp->exec_ctx);

	return rc;
}
