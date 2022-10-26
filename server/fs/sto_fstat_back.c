#include <spdk/likely.h>

#include "sto_fstat_back.h"

static int sto_fstat_exec(void *arg);
static void sto_fstat_exec_done(void *arg, int rc);

static struct sto_exec_ops fstat_ops = {
	.name = "fstat",
	.exec = sto_fstat_exec,
	.exec_done = sto_fstat_exec_done,
};

static void
sto_fstat_exec_done(void *arg, int rc)
{
	struct sto_fstat_back_req *req = arg;

	req->returncode = rc;
	req->fstat_back_done(req);
}

static int
sto_fstat_exec(void *arg)
{
	struct sto_fstat_back_req *req = arg;
	int rc = 0;

	rc = stat(req->filename, &req->stat);
	if (spdk_unlikely(rc == -1)) {
		printf("server: Failed to get stat for file %s: %s\n",
		       req->filename, strerror(errno));
		return -errno;
	}

	return 0;
}

static struct sto_fstat_back_req *
sto_fstat_back_alloc(const char *filename)
{
	struct sto_fstat_back_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("Cann't allocate memory for STO fstat req\n");
		return NULL;
	}

	req->filename = strdup(filename);
	if (spdk_unlikely(!req->filename)) {
		printf("Cann't allocate memory for filename: %s\n", filename);
		goto free_req;
	}

	sto_exec_init_ctx(&req->exec_ctx, &fstat_ops, req);

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_fstat_back_init_cb(struct sto_fstat_back_req *req,
		       fstat_back_done_t fstat_back_done, void *priv)
{
	req->fstat_back_done = fstat_back_done;
	req->priv = priv;
}

void
sto_fstat_back_free(struct sto_fstat_back_req *req)
{
	free((char *) req->filename);
	free(req);
}

static int
sto_fstat_back_submit(struct sto_fstat_back_req *req)
{
	return sto_exec(&req->exec_ctx);
}

int
sto_fstat_back(const char *filename, fstat_back_done_t fstat_back_done, void *priv)
{
	struct sto_fstat_back_req *req;
	int rc;

	req = sto_fstat_back_alloc(filename);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for back fstat\n");
		return -ENOMEM;
	}

	sto_fstat_back_init_cb(req, fstat_back_done, priv);

	rc = sto_fstat_back_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit back fstat, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_fstat_back_free(req);

	return rc;
}
