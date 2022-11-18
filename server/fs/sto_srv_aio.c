#include <spdk/json.h>
#include <spdk/util.h>
#include <spdk/likely.h>

#include "sto_exec.h"
#include "sto_srv_fs.h"
#include "sto_srv_aio.h"

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
