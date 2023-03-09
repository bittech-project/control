#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "sto_exec.h"
#include "sto_srv_fs.h"
#include "sto_srv_readdir.h"

static int sto_srv_readdir_exec(void *arg);
static void sto_srv_readdir_exec_done(void *arg, int rc);

static struct sto_exec_ops srv_readdir_ops = {
	.name = "readdir",
	.exec = sto_srv_readdir_exec,
	.exec_done = sto_srv_readdir_exec_done,
};

struct sto_srv_readdir_params {
	char *dirpath;
	bool skip_hidden;
};

static const struct spdk_json_object_decoder sto_srv_readdir_decoders[] = {
	{"dirpath", offsetof(struct sto_srv_readdir_params, dirpath), spdk_json_decode_string},
	{"skip_hidden", offsetof(struct sto_srv_readdir_params, skip_hidden), spdk_json_decode_bool},
};

struct sto_srv_readdir_req {
	struct sto_exec_ctx exec_ctx;

	struct sto_srv_readdir_params params;

	struct sto_srv_dirents dirents;

	void *cb_arg;
	sto_srv_readdir_done_t cb_fn;
};

static void sto_srv_readdir_req_free(struct sto_srv_readdir_req *req);

static void
sto_srv_readdir_exec_done(void *arg, int rc)
{
	struct sto_srv_readdir_req *req = arg;

	req->cb_fn(req->cb_arg, &req->dirents, rc);

	sto_srv_readdir_req_free(req);
}

static int
sto_srv_readdir_exec(void *arg)
{
	struct sto_srv_readdir_req *req = arg;
	struct sto_srv_readdir_params *params = &req->params;
	struct dirent *entry;
	DIR *dir;
	int rc = 0;

	dir = opendir(params->dirpath);
	if (spdk_unlikely(!dir)) {
		printf("server: Failed to open %s dir\n", params->dirpath);
		return -errno;
	}

	for (entry = readdir(dir); entry != NULL; entry = readdir(dir)) {
		struct sto_srv_dirent *dirent;

		if (params->skip_hidden && entry->d_name[0] == '.') {
			continue;
		}

		dirent = sto_srv_dirent_alloc(entry->d_name);
		if (spdk_unlikely(!dirent)) {
			sto_srv_dirents_free(&req->dirents);
			printf("server: Failed to alloc dirent\n");
			break;
		}

		rc = sto_srv_dirent_get_stat(dirent, params->dirpath);
		if (spdk_unlikely(rc)) {
			sto_srv_dirent_free(dirent);
			sto_srv_dirents_free(&req->dirents);
			printf("server: Failed to dirent to get stat\n");
			break;
		}

		sto_srv_dirents_add(&req->dirents, dirent);
	}

	rc = closedir(dir);
	if (spdk_unlikely(rc == -1)) {
		sto_srv_dirents_free(&req->dirents);
		printf("server: Failed to close %s dir\n", params->dirpath);
		rc = -errno;
	}

	return rc;
}

static struct sto_srv_readdir_req *
sto_srv_readdir_req_alloc(const struct spdk_json_val *params)
{
	struct sto_srv_readdir_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("server: Cann't allocate memory for readdir req\n");
		return NULL;
	}

	if (spdk_json_decode_object(params, sto_srv_readdir_decoders,
				    SPDK_COUNTOF(sto_srv_readdir_decoders), &req->params)) {
		printf("server: Cann't decode readdir req params\n");
		goto free_req;
	}

	sto_exec_init_ctx(&req->exec_ctx, &srv_readdir_ops, req);

	sto_srv_dirents_init(&req->dirents);

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_srv_readdir_req_init_cb(struct sto_srv_readdir_req *req,
			    sto_srv_readdir_done_t cb_fn, void *cb_arg)
{
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
}

static void
sto_srv_readdir_req_free(struct sto_srv_readdir_req *req)
{
	spdk_json_free_object(sto_srv_readdir_decoders,
			      SPDK_COUNTOF(sto_srv_readdir_decoders), &req->params);
	sto_srv_dirents_free(&req->dirents);
	free(req);
}

static int
sto_srv_readdir_req_submit(struct sto_srv_readdir_req *req)
{
	return sto_exec(&req->exec_ctx);
}

int
sto_srv_readdir(const struct spdk_json_val *params,
		struct sto_srv_readdir_args *args)
{
	struct sto_srv_readdir_req *req;
	int rc;

	req = sto_srv_readdir_req_alloc(params);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for readdir req\n");
		return -ENOMEM;
	}

	sto_srv_readdir_req_init_cb(req, args->cb_fn, args->cb_arg);

	rc = sto_srv_readdir_req_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit readdir, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_srv_readdir_req_free(req);

	return rc;
}
