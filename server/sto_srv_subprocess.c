#include <spdk/stdinc.h>

#include <spdk/likely.h>

#include "sto_srv_subprocess.h"
#include "sto_exec.h"

static int sto_srv_subprocess_exec(void *arg);
static void sto_srv_subprocess_exec_done(void *arg, int rc);

static struct sto_exec_ops srv_subprocess_ops = {
	.name = "subprocess",
	.exec = sto_srv_subprocess_exec,
	.exec_done = sto_srv_subprocess_exec_done,
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
sto_srv_subprocess_wait(pid_t pid, int *result)
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
sto_srv_subprocess_exec(void *arg)
{
	struct sto_srv_subprocess_req *req = arg;
	pid_t pid;
	int rc, result = 0;

	if (req->capture_output) {
		int rc = pipe(req->pipefd);
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
		if (req->capture_output) {
			rc = __setup_child_pipe(req->pipefd);
		} else {
			__redirect_to_null();
		}

		/*
		 * execvp() takes (char *const *) for backward compatibility,
		 * but POSIX guarantees that it will not modify the strings,
		 * so the cast is safe
		 */
		rc = execvp(req->file, (char *const *) req->args);
		if (rc == -1) {
			printf("Failed to execvp: %s", strerror(errno));
		}

		exit(-errno);
	}

	/* Parent */
	rc = sto_srv_subprocess_wait(pid, &result);
	if (spdk_unlikely(rc)) {
		printf("Failed to wait for child process pid=%d, rc=%d\n",
		       pid, rc);
	}

	req->returncode = result;

	return rc;
}

static void
sto_srv_subprocess_exec_done(void *arg, int rc)
{
	struct sto_srv_subprocess_req *req = arg;

	if (req->capture_output) {
		memset(req->output, 0, sizeof(req->output));

		read(req->pipefd[STDIN_FILENO], req->output, sizeof(req->output) - 1);
	}

	req->done(req);
}

struct sto_srv_subprocess_req *
sto_srv_subprocess_req_alloc(const char *const argv[], int numargs, bool capture_output)
{
	struct sto_srv_subprocess_req *req;
	unsigned int data_len;
	int real_numargs = numargs + 1; /* Plus one for the NULL terminator at the end */
	int i;

	if (spdk_unlikely(!numargs)) {
		printf("Too few arguments\n");
		return NULL;
	}

	/* Count the number of bytes for the 'real_numargs' arguments to be allocated */
	data_len = real_numargs * sizeof(char *);

	req = calloc(1, sizeof(*req) + data_len);
	if (spdk_unlikely(!req)) {
		printf("Cann't allocate memory for subprocess\n");
		return NULL;
	}

	sto_exec_init_ctx(&req->exec_ctx, &srv_subprocess_ops, req);

	req->capture_output = capture_output;
	req->numargs = real_numargs;

	req->file = argv[0];

	for (i = 0; i < req->numargs - 1; i++) {
		req->args[i] = argv[i];
	}

	req->args[req->numargs - 1] = NULL;

	return req;
}

void
sto_srv_subprocess_req_init_cb(struct sto_srv_subprocess_req *req,
			       sto_srv_subprocess_done_t done, void *priv)
{
	req->done = done;
	req->priv = priv;
}

void
sto_srv_subprocess_req_free(struct sto_srv_subprocess_req *req)
{
	free(req);
}

int
sto_srv_subprocess_req_submit(struct sto_srv_subprocess_req *req)
{
	return sto_exec(&req->exec_ctx);
}

int
sto_srv_subprocess(const char *const argv[], int numargs, bool capture_output,
		   sto_srv_subprocess_done_t done, void *priv)
{
	struct sto_srv_subprocess_req *req;
	int rc;

	req = sto_srv_subprocess_req_alloc(argv, numargs, capture_output);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for subprocess req\n");
		return -ENOMEM;
	}

	sto_srv_subprocess_req_init_cb(req, done, priv);

	rc = sto_srv_subprocess_req_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit subprocess, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_srv_subprocess_req_free(req);

	return rc;
}
