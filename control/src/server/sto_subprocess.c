#include <spdk/stdinc.h>

#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_subprocess.h"
#include "sto_exec.h"

static int sto_subprocess_exec(void *arg);
static void sto_subprocess_exec_done(void *arg, int rc);

static struct sto_exec_ops subprocess_ops = {
	.name = "subprocess",
	.exec = sto_subprocess_exec,
	.exec_done = sto_subprocess_exec_done,
};

static int
__redirect_to_null(void)
{
	int fd, rc;

	fd = open("/dev/null", O_WRONLY);
	if (spdk_unlikely(fd == -1)) {
		printf("Failed to open /dev/null: %s\n",
		       strerror(errno));
		return -errno;
	}

	rc = dup2(fd, 1);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to dup2 stdout: %s\n",
		       strerror(errno));
		return -errno;
	}

	rc = dup2(fd, 2);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to dup2 stderr: %s\n",
		       strerror(errno));
		return -errno;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to close /dev/null: %s",
		       strerror(errno));
		return -errno;
	}

	return 0;
}

static int
__setup_child_pipe(int pipefd[2])
{
	int rc;

	/* close read end of pipe */
	rc = close(pipefd[STDIN_FILENO]);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to child close STDIN: %s",
		       strerror(errno));
		return -errno;
	}

	/* make STDOUT_FILENO same as write-to end of pipe */
	rc = dup2(pipefd[STDOUT_FILENO], STDOUT_FILENO);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to child dup2 STDOUT: %s",
		       strerror(errno));
		return -errno;
	}

	/* close excess fildes */
	rc = close(pipefd[STDOUT_FILENO]);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to child close pipe: %s",
		       strerror(errno));
		return -errno;
	}

	return 0;
}

static int
sto_subprocess_wait(pid_t pid, int *result)
{
	int ret, status;

	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		printf("waitpid: %s\n", strerror(errno));
		return -errno;
	}

	if (WIFSIGNALED(status)) {
		*result = WTERMSIG(status);
		return -EINTR;
	}

	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status)) {
			*result = WEXITSTATUS(status);
		}

		return 0;
	}

	return -EFAULT;
}

static int
sto_subprocess_exec(void *arg)
{
	struct sto_subprocess *subp = arg;
	pid_t pid;
	int rc, result = 0;

	if (subp->capture_output) {
		int rc = pipe(subp->pipefd);
		if (spdk_unlikely(rc == -1)) {
			printf("Failed to create subprocess pipe: %s\n",
			       strerror(errno));
			return -errno;
		}
	}

	pid = fork();
	if (spdk_unlikely(pid == -1)) {
		printf("Failed to fork: %s\n", strerror(errno));
		return -errno;
	}

	/* Child */
	if (!pid) {
		if (subp->capture_output) {
			rc = __setup_child_pipe(subp->pipefd);
		} else {
			__redirect_to_null();
		}

		/*
		 * execvp() takes (char *const *) for backward compatibility,
		 * but POSIX guarantees that it will not modify the strings,
		 * so the cast is safe
		 */
		rc = execvp(subp->file, (char *const *) subp->args);
		if (rc == -1) {
			printf("Failed to execvp: %s", strerror(errno));
		}

		exit(-errno);
	}

	/* Parent */
	rc = sto_subprocess_wait(pid, &result);
	if (spdk_unlikely(rc)) {
		printf("Failed to wait for child process pid=%d, rc=%d\n",
		       pid, rc);
	}

	subp->returncode = result;

	return rc;
}

static void
sto_subprocess_exec_done(void *arg, int rc)
{
	struct sto_subprocess *subp = arg;

	if (subp->capture_output) {
		memset(subp->output, 0, sizeof(subp->output));

		read(subp->pipefd[STDIN_FILENO], subp->output, sizeof(subp->output) - 1);
	}

	subp->subprocess_done(subp);
}

struct sto_subprocess *
sto_subprocess_alloc(const char *const argv[], int numargs, bool capture_output)
{
	struct sto_subprocess *subp;
	unsigned int data_len;
	int real_numargs = numargs + 1; /* Plus one for the NULL terminator at the end */
	int i;

	if (spdk_unlikely(!numargs)) {
		printf("Too few arguments\n");
		return NULL;
	}

	/* Count the number of bytes for the 'real_numargs' arguments to be allocated */
	data_len = real_numargs * sizeof(char *);

	subp = calloc(1, sizeof(*subp) + data_len);
	if (spdk_unlikely(!subp)) {
		printf("Cann't allocate memory for subprocess\n");
		return NULL;
	}

	sto_exec_init_ctx(&subp->exec_ctx, &subprocess_ops, subp);

	subp->capture_output = capture_output;
	subp->numargs = real_numargs;

	subp->file = argv[0];

	for (i = 0; i < subp->numargs - 1; i++) {
		subp->args[i] = argv[i];
	}

	subp->args[subp->numargs - 1] = NULL;

	return subp;
}

void
sto_subprocess_init_cb(struct sto_subprocess *subp,
		       subprocess_done_t subprocess_done, void *priv)
{
	subp->subprocess_done = subprocess_done;
	subp->priv = priv;
}

void
sto_subprocess_free(struct sto_subprocess *subp)
{
	free(subp);
}

int
sto_subprocess_run(struct sto_subprocess *subp)
{
	int rc = 0;

	rc = sto_exec(&subp->exec_ctx);

	return rc;
}
