#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/util.h>
#include <spdk/likely.h>

#include "sto_exec.h"
#include "sto_srv_fs.h"
#include "sto_srv_aio.h"
#include "sto_async.h"

struct sto_srv_writefile_params {
	char *filepath;
	int oflag;
	char *buf;
};

static const struct spdk_json_object_decoder sto_srv_writefile_decoders[] = {
	{"filepath", offsetof(struct sto_srv_writefile_params, filepath), spdk_json_decode_string},
	{"oflag", offsetof(struct sto_srv_writefile_params, oflag), spdk_json_decode_int32},
	{"buf", offsetof(struct sto_srv_writefile_params, buf), spdk_json_decode_string},
};

struct sto_srv_writefile_req {
	struct sto_exec_ctx exec_ctx;

	struct sto_srv_writefile_params params;

	void *cb_arg;
	sto_generic_cb cb_fn;
};

static int sto_srv_writefile_exec(void *arg);
static void sto_srv_writefile_exec_done(void *arg, int rc);

static struct sto_exec_ops srv_writefile_ops = {
	.name = "writefile",
	.exec = sto_srv_writefile_exec,
	.exec_done = sto_srv_writefile_exec_done,
};

static struct sto_srv_writefile_req *
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
			      sto_generic_cb cb_fn, void *cb_arg)
{
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
}

static void
sto_srv_writefile_req_free(struct sto_srv_writefile_req *req)
{
	spdk_json_free_object(sto_srv_writefile_decoders,
			      SPDK_COUNTOF(sto_srv_writefile_decoders), &req->params);
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

	return sto_write_file(params->filepath, params->oflag, params->buf, strlen(params->buf));
}

static void
sto_srv_writefile_exec_done(void *arg, int rc)
{
	struct sto_srv_writefile_req *req = arg;

	req->cb_fn(req->cb_arg, rc);

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

	sto_srv_writefile_req_init_cb(req, args->cb_fn, args->cb_arg);

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

struct sto_srv_readfile_params {
	char *filepath;
	uint32_t size;
};

static const struct spdk_json_object_decoder sto_srv_readfile_decoders[] = {
	{"filepath", offsetof(struct sto_srv_readfile_params, filepath), spdk_json_decode_string},
	{"size", offsetof(struct sto_srv_readfile_params, size), spdk_json_decode_uint32},
};

struct sto_srv_readfile_req {
	struct sto_exec_ctx exec_ctx;

	struct sto_srv_readfile_params params;
	char *buf;

	void *cb_arg;
	sto_srv_readfile_done_t cb_fn;
};

static int sto_srv_readfile_exec(void *arg);
static void sto_srv_readfile_exec_done(void *arg, int rc);

static struct sto_exec_ops srv_readfile_ops = {
	.name = "readfile",
	.exec = sto_srv_readfile_exec,
	.exec_done = sto_srv_readfile_exec_done,
};

static struct sto_srv_readfile_req *
sto_srv_readfile_req_alloc(const struct spdk_json_val *params)
{
	struct sto_srv_readfile_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("server: Cann't allocate memory for readfile req\n");
		return NULL;
	}

	if (spdk_json_decode_object(params, sto_srv_readfile_decoders,
				    SPDK_COUNTOF(sto_srv_readfile_decoders), &req->params)) {
		printf("server: Cann't decode readfile req params\n");
		goto free_req;
	}

	sto_exec_init_ctx(&req->exec_ctx, &srv_readfile_ops, req);

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_srv_readfile_req_init_cb(struct sto_srv_readfile_req *req,
			     sto_srv_readfile_done_t cb_fn, void *cb_arg)
{
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
}

static void
sto_srv_readfile_req_free(struct sto_srv_readfile_req *req)
{
	spdk_json_free_object(sto_srv_readfile_decoders,
			      SPDK_COUNTOF(sto_srv_readfile_decoders), &req->params);
	free(req->buf);
	free(req);
}

static int
sto_srv_readfile_req_submit(struct sto_srv_readfile_req *req)
{
	return sto_exec(&req->exec_ctx);
}

static int
sto_srv_readfile_exec(void *arg)
{
	struct sto_srv_readfile_req *req = arg;
	struct sto_srv_readfile_params *params = &req->params;

	if (!params->size) {
		struct stat sb;

		if (lstat(params->filepath, &sb) == -1) {
			printf("server: Failed to get stat for file %s: %s\n",
			       params->filepath, strerror(errno));
			return -errno;
		}

		params->size = sb.st_size;
	}

	req->buf = calloc(1, params->size);
	if (spdk_unlikely(!req->buf)) {
		printf("server: Failed to alloc buf to read: size=%u\n", params->size);
		return -ENOMEM;
	}

	return sto_read_file(params->filepath, req->buf, params->size);
}

static void
sto_srv_readfile_exec_done(void *arg, int rc)
{
	struct sto_srv_readfile_req *req = arg;
	char *buf = !rc ? req->buf : "";

	req->cb_fn(req->cb_arg, buf, rc);
	sto_srv_readfile_req_free(req);
}

int
sto_srv_readfile(const struct spdk_json_val *params,
		 struct sto_srv_readfile_args *args)
{
	struct sto_srv_readfile_req *req;
	int rc;

	req = sto_srv_readfile_req_alloc(params);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for readfile req\n");
		return -ENOMEM;
	}

	sto_srv_readfile_req_init_cb(req, args->cb_fn, args->cb_arg);

	rc = sto_srv_readfile_req_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit readfile req, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_srv_readfile_req_free(req);

	return rc;
}

struct sto_srv_readlink_params {
	char *filepath;
};

static const struct spdk_json_object_decoder sto_srv_readlink_decoders[] = {
	{"filepath", offsetof(struct sto_srv_readlink_params, filepath), spdk_json_decode_string},
};

struct sto_srv_readlink_req {
	struct sto_exec_ctx exec_ctx;

	struct sto_srv_readlink_params params;
	char *buf;

	void *cb_arg;
	sto_srv_readlink_done_t cb_fn;
};

static int sto_srv_readlink_exec(void *arg);
static void sto_srv_readlink_exec_done(void *arg, int rc);

static struct sto_exec_ops srv_readlink_ops = {
	.name = "readlink",
	.exec = sto_srv_readlink_exec,
	.exec_done = sto_srv_readlink_exec_done,
};

static struct sto_srv_readlink_req *
sto_srv_readlink_req_alloc(const struct spdk_json_val *params)
{
	struct sto_srv_readlink_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("server: Cann't allocate memory for readlink req\n");
		return NULL;
	}

	if (spdk_json_decode_object(params, sto_srv_readlink_decoders,
				    SPDK_COUNTOF(sto_srv_readlink_decoders), &req->params)) {
		printf("server: Cann't decode readlink req params\n");
		goto free_req;
	}

	sto_exec_init_ctx(&req->exec_ctx, &srv_readlink_ops, req);

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_srv_readlink_req_init_cb(struct sto_srv_readlink_req *req,
			     sto_srv_readlink_done_t cb_fn, void *cb_arg)
{
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
}

static void
sto_srv_readlink_req_free(struct sto_srv_readlink_req *req)
{
	spdk_json_free_object(sto_srv_readlink_decoders,
			      SPDK_COUNTOF(sto_srv_readlink_decoders), &req->params);
	free(req->buf);
	free(req);
}

static int
sto_srv_readlink_req_submit(struct sto_srv_readlink_req *req)
{
	return sto_exec(&req->exec_ctx);
}

static int
sto_srv_readlink_exec(void *arg)
{
	struct sto_srv_readlink_req *req = arg;
	struct sto_srv_readlink_params *params = &req->params;
	struct stat sb;
	ssize_t size, res;

	if (lstat(params->filepath, &sb) == -1) {
		printf("server: Failed to get stat for file %s: %s\n",
		       params->filepath, strerror(errno));
		return -errno;
	}

	size = (sb.st_size ?: PATH_MAX) + 1;

	req->buf = calloc(1, size);
	if (spdk_unlikely(!req->buf)) {
		printf("server: Failed to alloc buf to read: size=%zd\n", size);
		return -ENOMEM;
	}

	res = readlink(params->filepath, req->buf, size);
	if (spdk_unlikely(res == -1)) {
		printf("server: Failed to readlink %s\n", params->filepath);
		return -errno;
	}

	return 0;
}

static void
sto_srv_readlink_exec_done(void *arg, int rc)
{
	struct sto_srv_readlink_req *req = arg;
	char *buf = !rc ? req->buf : "";

	req->cb_fn(req->cb_arg, buf, rc);
	sto_srv_readlink_req_free(req);
}

int
sto_srv_readlink(const struct spdk_json_val *params,
		 struct sto_srv_readlink_args *args)
{
	struct sto_srv_readlink_req *req;
	int rc;

	req = sto_srv_readlink_req_alloc(params);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for readlink req\n");
		return -ENOMEM;
	}

	sto_srv_readlink_req_init_cb(req, args->cb_fn, args->cb_arg);

	rc = sto_srv_readlink_req_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit readlink req, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_srv_readlink_req_free(req);

	return rc;
}
