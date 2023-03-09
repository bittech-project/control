#include "sto_srv_subprocess.h"

#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "sto_exec.h"

static int sto_srv_subprocess_exec(void *arg);
static void sto_srv_subprocess_exec_done(void *arg, int rc);

static struct sto_exec_ops srv_subprocess_ops = {
	.name = "subprocess",
	.exec = sto_srv_subprocess_exec,
	.exec_done = sto_srv_subprocess_exec_done,
};

#define STO_SUBPROCESS_MAX_ARGS 128

struct sto_srv_subprocess_arg_list {
	const char *args[STO_SUBPROCESS_MAX_ARGS + 1];
	size_t numargs;
};

static int
sto_srv_subprocess_cmd_decode(const struct spdk_json_val *val, void *out)
{
	struct sto_srv_subprocess_arg_list *arg_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, arg_list->args, STO_SUBPROCESS_MAX_ARGS,
				      &arg_list->numargs, sizeof(char *));
}

static void
sto_srv_subprocess_cmd_free(struct sto_srv_subprocess_arg_list *arg_list)
{
	size_t i;

	for (i = 0; i < arg_list->numargs; i++) {
		free((char *) arg_list->args[i]);
	}
}

struct sto_srv_subprocess_params {
	struct sto_srv_subprocess_arg_list arg_list;
	bool capture_output;
};

static void
sto_srv_subprocess_params_free(struct sto_srv_subprocess_params *params)
{
	sto_srv_subprocess_cmd_free(&params->arg_list);
}

static const struct spdk_json_object_decoder sto_srv_subprocess_decoders[] = {
	{"cmd", offsetof(struct sto_srv_subprocess_params, arg_list), sto_srv_subprocess_cmd_decode},
	{"capture_output", offsetof(struct sto_srv_subprocess_params, capture_output), spdk_json_decode_bool, true},
};

struct sto_srv_subprocess_req {
	struct sto_exec_ctx exec_ctx;

	int pipefd[2];

	char output[256];
	size_t output_sz;

	struct sto_srv_subprocess_params params;

	void *cb_arg;
	sto_srv_subprocess_done_t cb_fn;
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
	struct sto_srv_subprocess_params *params = &req->params;
	pid_t pid;
	int rc = 0, result = 0;

	if (params->capture_output) {
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
		struct sto_srv_subprocess_arg_list *arg_list = &params->arg_list;

		if (params->capture_output) {
			rc = __setup_child_pipe(req->pipefd);
		} else {
			__redirect_to_null();
		}

		/*
		 * execvp() takes (char *const *) for backward compatibility,
		 * but POSIX guarantees that it will not modify the strings,
		 * so the cast is safe
		 */
		rc = execvp(arg_list->args[0], (char *const *) arg_list->args);
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
		return rc;
	}

	return result;
}

static void sto_srv_subprocess_req_free(struct sto_srv_subprocess_req *req);

static void
sto_srv_subprocess_exec_done(void *arg, int rc)
{
	struct sto_srv_subprocess_req *req = arg;
	struct sto_srv_subprocess_params *params = &req->params;
	char *output = NULL;

	if (params->capture_output) {
		memset(req->output, 0, sizeof(req->output));

		read(req->pipefd[STDIN_FILENO], req->output, sizeof(req->output) - 1);

		output = req->output;
	}

	req->cb_fn(req->cb_arg, output, rc);

	sto_srv_subprocess_req_free(req);
}

static struct sto_srv_subprocess_req *
sto_srv_subprocess_req_alloc(const struct spdk_json_val *params)
{
	struct sto_srv_subprocess_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("Cann't allocate memory for subprocess req\n");
		return NULL;
	}

	if (spdk_json_decode_object(params, sto_srv_subprocess_decoders,
				    SPDK_COUNTOF(sto_srv_subprocess_decoders), &req->params)) {
		printf("server: Cann't decode subprocess req params\n");
		goto free_req;
	}

	sto_exec_init_ctx(&req->exec_ctx, &srv_subprocess_ops, req);

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_srv_subprocess_req_init_cb(struct sto_srv_subprocess_req *req,
			       sto_srv_subprocess_done_t cb_fn, void *cb_arg)
{
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
}

static void
sto_srv_subprocess_req_free(struct sto_srv_subprocess_req *req)
{
	sto_srv_subprocess_params_free(&req->params);

	free(req);
}

static int
sto_srv_subprocess_req_submit(struct sto_srv_subprocess_req *req)
{
	return sto_exec(&req->exec_ctx);
}

int
sto_srv_subprocess(const struct spdk_json_val *params,
		   struct sto_srv_subprocess_args *args)
{
	struct sto_srv_subprocess_req *req;
	int rc;

	req = sto_srv_subprocess_req_alloc(params);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for subprocess req\n");
		return -ENOMEM;
	}

	sto_srv_subprocess_req_init_cb(req, args->cb_fn, args->cb_arg);

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
