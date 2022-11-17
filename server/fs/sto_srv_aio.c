#include <spdk/json.h>
#include <spdk/util.h>
#include <spdk/likely.h>

#include "sto_srv_aio.h"

static void
sto_stdout_choker(int action)
{
	static int STO_STDOUT_FD = 0;
	static FILE *STO_STDOUT_FNULL = NULL;

	switch (action) {
	case 1:
		STO_STDOUT_FD = dup(STDOUT_FILENO);

		fflush(stdout);
		STO_STDOUT_FNULL = fopen("/dev/null", "w");
		dup2(fileno(STO_STDOUT_FNULL), STDOUT_FILENO);
		break;
	case 0:
		dup2(STO_STDOUT_FD, STDOUT_FILENO);
		close(STO_STDOUT_FD);
		break;
	default:
		printf("Invalid action - %d\n", action);
		break;
	}
}

static void
sto_stderr_choker(int action)
{
	static int STO_STDERR_FD = 0;
	static FILE *STO_STDERR_FNULL = NULL;

	switch (action) {
	case 1:
		STO_STDERR_FD = dup(STDERR_FILENO);

		fflush(stderr);
		STO_STDERR_FNULL = fopen("/dev/null", "w");
		dup2(fileno(STO_STDERR_FNULL), STDERR_FILENO);
		break;
	case 0:
		dup2(STO_STDERR_FD, STDERR_FILENO);
		close(STO_STDERR_FD);
		break;
	default:
		printf("Invalid action - %d\n", action);
		break;
	}
}

void
sto_choker_on(void)
{
	sto_stdout_choker(1);
	sto_stderr_choker(1);
}

void
sto_choker_off(void)
{
	sto_stdout_choker(0);
	sto_stderr_choker(0);
}

int
sto_read(int fd, void *data, size_t size)
{
	ssize_t ret;
	int rc = 0;

	while (size) {
		ret = read(fd, data, size);
		if (ret == -1) {
			if (errno == EINTR) {
				continue;
			}

			printf("Failed to read from %d fd: %s\n",
			       fd, strerror(errno));
			rc = -errno;
			break;
		}

		if (!ret) {
			break;
		}

		data += ret;
		size -= ret;
	}

	return rc;
}

int
sto_write(int fd, void *data, size_t size)
{
	ssize_t ret;
	int rc = 0;

	while (size) {
		ret = write(fd, data, size);
		if (ret == -1) {
			if (errno == EINTR) {
				continue;
			}

			printf("Failed to write to %d fd: %s\n",
			       fd, strerror(errno));
			rc = -errno;
			break;
		}

		if (!ret) {
			break;
		}

		data += ret;
		size -= ret;
	}

	return rc;
}

int
sto_read_file(const char *filepath, void *data, size_t size)
{
	int fd, rc;

	fd = open(filepath, O_RDONLY);
	if (spdk_unlikely(fd == -1)) {
		printf("Failed to open %s file\n", filepath);
		return -errno;
	}

	rc = sto_read(fd, data, size);
	if (spdk_unlikely(rc)) {
		printf("Failed to read %s file\n", filepath);
		return rc;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to close %s file\n", filepath);
	}

	return 0;
}

int
sto_write_file(const char *filepath, void *data, size_t size)
{
	int fd, rc;

	fd = open(filepath, O_WRONLY);
	if (spdk_unlikely(fd == -1)) {
		printf("Failed to open %s file\n", filepath);
		return -errno;
	}

	rc = sto_write(fd, data, size);
	if (spdk_unlikely(rc)) {
		printf("Failed to write %s file\n", filepath);
		return rc;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to close %s file\n", filepath);
	}

	return 0;
}

struct sto_srv_writefile_params {
	char *filepath;
	char *buf;
};

static void
sto_srv_writefile_params_free(struct sto_srv_writefile_params *params)
{
	free(params->filepath);
	free(params->buf);
}

static const struct spdk_json_object_decoder sto_srv_writefile_decoders[] = {
	{"filepath", offsetof(struct sto_srv_writefile_params, filepath), spdk_json_decode_string},
	{"buf", offsetof(struct sto_srv_writefile_params, buf), spdk_json_decode_string},
};

struct sto_srv_writefile_req {
	struct sto_exec_ctx exec_ctx;

	struct sto_srv_writefile_params params;

	void *priv;
	sto_srv_writefile_done_t done;
};

static int sto_srv_writefile_exec(void *arg);
static void sto_srv_writefile_exec_done(void *arg, int rc);

static struct sto_exec_ops srv_writefile_ops = {
	.name = "writefile",
	.exec = sto_srv_writefile_exec,
	.exec_done = sto_srv_writefile_exec_done,
};

struct sto_srv_writefile_req *
sto_srv_writefile_req_alloc(const struct spdk_json_val *params)
{
	struct sto_srv_writefile_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("server: Cann't allocate memory for writefile req\n");
		return NULL;
	}

	if (spdk_json_decode_object(params, sto_srv_writefile_decoders,
				    SPDK_COUNTOF(sto_srv_writefile_decoders), &req->params)) {
		printf("server: Cann't decode writefile req params\n");
		goto free_req;
	}

	sto_exec_init_ctx(&req->exec_ctx, &srv_writefile_ops, req);

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_srv_writefile_req_init_cb(struct sto_srv_writefile_req *req,
			      sto_srv_writefile_done_t done, void *priv)
{
	req->done = done;
	req->priv = priv;
}

static void
sto_srv_writefile_req_free(struct sto_srv_writefile_req *req)
{
	sto_srv_writefile_params_free(&req->params);
	free(req);
}

static int
sto_srv_writefile_req_submit(struct sto_srv_writefile_req *req)
{
	return sto_exec(&req->exec_ctx);
}

static int
sto_srv_writefile_exec(void *arg)
{
	struct sto_srv_writefile_req *req = arg;
	struct sto_srv_writefile_params *params = &req->params;

	return sto_write_file(params->filepath, params->buf, strlen(params->buf));
}

static void
sto_srv_writefile_exec_done(void *arg, int rc)
{
	struct sto_srv_writefile_req *req = arg;

	req->done(req->priv, rc);
	sto_srv_writefile_req_free(req);
}

int
sto_srv_writefile(const struct spdk_json_val *params,
		  struct sto_srv_writefile_args *args)
{
	struct sto_srv_writefile_req *req;
	int rc;

	req = sto_srv_writefile_req_alloc(params);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for writefile req\n");
		return -ENOMEM;
	}

	sto_srv_writefile_req_init_cb(req, args->done, args->priv);

	rc = sto_srv_writefile_req_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit writefile req, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_srv_writefile_req_free(req);

	return rc;
}
