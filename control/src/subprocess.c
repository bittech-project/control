#include <errno.h>  /* errno */
#include <stdlib.h> /* NULL */
#include <string.h> /* strerror */
#include <unistd.h> /* readlink */

#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/thread.h>

#include <rte_malloc.h>

#include "subprocess.h"

#define STO_SUBPROCESS_POLL_PERIOD	4000 /* 4ms */

static struct spdk_poller *sto_subprocess_poller;

TAILQ_HEAD(sto_subprocess_list, sto_subprocess);

static struct sto_subprocess_list sto_subprocess_list =
	TAILQ_HEAD_INITIALIZER(sto_subprocess_list);

static int subprocess_initialized;

static int
sto_subprocess_poll(void *arg)
{
	struct sto_subprocess *subp, *tmp;

	TAILQ_FOREACH_SAFE(subp, &sto_subprocess_list, list, tmp) {
		int status;
		pid_t return_pid;

		return_pid = waitpid(subp->pid, &status, WNOHANG);

		if (return_pid == 0) {
			/* child is still running */
			continue;
		}

		if (return_pid == subp->pid || return_pid == -1) {
			/* child is finished. exit status in status */
			TAILQ_REMOVE(&sto_subprocess_list, subp, list);
			subp->release(subp, status);
		}
	}
}

int
sto_subprocess_init(void)
{
	if (spdk_unlikely(subprocess_initialized)) {
		SPDK_ERRLOG("STO subprocess lib has already been initialized\n");
		return -EINVAL;
	}

	sto_subprocess_poller = SPDK_POLLER_REGISTER(sto_subprocess_poll,
				NULL, STO_SUBPROCESS_POLL_PERIOD);
	if (spdk_unlikely(!sto_subprocess_poller)) {
		SPDK_ERRLOG("Cann't register the STO subprocess poller\n");
		return -EFAULT;
	}

	subprocess_initialized = 1;

	return 0;
}

void
sto_subprocess_exit(void)
{
	if (spdk_unlikely(!subprocess_initialized)) {
		SPDK_ERRLOG("STO subprocess lib hasn't been initialized yet\n");
		return;
	}

	spdk_poller_unregister(&sto_subprocess_poller);
	subprocess_initialized = 0;
}

static void
sto_subprocess_release(struct sto_subprocess *subp, int status)
{
	struct sto_subprocess_ctx *subp_ctx = subp->subp_ctx;

	subp_ctx->returncode = status;

	if (subp->capture_output) {
		ssize_t read_sz;

		memset(subp_ctx->output, 0, sizeof(subp_ctx->output));

		read_sz = read(0, subp_ctx->output, sizeof(subp_ctx->output) - 1);
	}

	sto_subprocess_destroy(subp);

	subp_ctx->subprocess_done(subp_ctx);
}

struct sto_subprocess *
sto_subprocess_create(const char *const argv[], int numargs,
		      bool capture_output, uint64_t timeout)
{
	struct sto_subprocess *subp;
	int i;

	if (spdk_unlikely(!subprocess_initialized)) {
		SPDK_ERRLOG("STO subprocess lib hasn't been initialized yet\n");
		return NULL;
	}

	if (spdk_unlikely(!numargs)) {
		SPDK_ERRLOG("Too few arguments\n");
		return NULL;
	}

	subp = rte_zmalloc(NULL, sizeof(*subp) + (numargs * sizeof(char *)), 0);
	if (spdk_unlikely(!subp)) {
		SPDK_ERRLOG("Cann't allocate memory for subprocess\n");
		return NULL;
	}

	subp->capture_output = capture_output;
	subp->numargs = numargs;

	subp->file = argv[0];

	for (i = 0; i < numargs; i++) {
		subp->args[i] = argv[i];
	}

	subp->release = sto_subprocess_release;

	return subp;
}

void
sto_subprocess_destroy(struct sto_subprocess *subp)
{
	rte_free(subp);
}

static int
__redirect_to_null(void)
{
	int fd, rc;

	fd = open("/dev/null", O_WRONLY);
	if (spdk_unlikely(fd == -1)) {
		SPDK_ERRLOG("Failed to open /dev/null: %s\n",
			    strerror(errno));
		return errno;
	}

	rc = dup2(fd, 1);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to dup2 stdout: %s\n",
			    strerror(errno));
		return errno;
	}

	rc = dup2(fd, 2);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to dup2 stderr: %s\n",
			    strerror(errno));
		return errno;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to close /dev/null: %s",
			    strerror(errno));
		return errno;
	}

	return 0;
}

static int
__setup_pipe(int pipefd[2], int dir)
{
	int rc;

	/* close read/write end of pipe */
	rc = close(pipefd[!dir]);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to child close (pipefd[%d]): %s",
			    !dir, strerror(errno));
		return errno;
	}

	/* make 0/1 same as read/write-to end of pipe */
	rc = dup2(pipefd[dir], dir);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to child dup2 (pipefd[%d]): %s",
			    dir, strerror(errno));
		return errno;
	}

	/* close excess fildes */
	rc = close(pipefd[dir]);
	if (spdk_unlikely(rc == -1)) {
		SPDK_ERRLOG("Failed to child close (pipefd[%d]): %s",
			    dir, strerror(errno));
		return errno;
	}

	return 0;
}

int
sto_subprocess_run(struct sto_subprocess *subp,
		   struct sto_subprocess_ctx *subp_ctx)
{
	int pipefd[2];
	pid_t pid;
	int rc = 0;

	if (subp->capture_output) {
		rc = pipe(pipefd);
		if (spdk_unlikely(rc == -1)) {
			SPDK_ERRLOG("Failed to create pipe: %s\n",
				    strerror(errno));
			return errno;
		}
	}

	pid = fork();
	if (spdk_unlikely(pid == -1)) {
		SPDK_ERRLOG("Failed to fork: %s\n",
			    strerror(errno));
		return errno;
	}

	/* Child */
	if (!pid) {
		if (subp->capture_output) {
			rc = __setup_pipe(pipefd, STDOUT_FILENO);
		} else {
			rc = __redirect_to_null();
		}

		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to set up child process\n");
		}

		/*
		 * execvp() takes (char *const *) for backward compatibility,
		 * but POSIX guarantees that it will not modify the strings,
		 * so the cast is safe
		 */
		rc = execvp(subp->file, (char *const *) subp->args);
		if (rc == -1) {
			SPDK_ERRLOG("Failed to child execvp: %s", strerror(errno));
		}

		exit(0);
	}

	/* Parent */
	if (subp->capture_output) {
		rc = __setup_pipe(pipefd, STDIN_FILENO);
	}

	subp->pid = pid;
	subp->subp_ctx = subp_ctx;
	TAILQ_INSERT_TAIL(&sto_subprocess_list, subp, list);

	return 0;
}
