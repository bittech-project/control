#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "sto_client.h"
#include "sto_fstat_front.h"

static struct sto_fstat_req *
sto_fstat_alloc(const char *filename)
{
	struct sto_fstat_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for STO fstat req\n");
		return NULL;
	}

	req->filename = strdup(filename);
	if (spdk_unlikely(!req->filename)) {
		SPDK_ERRLOG("Cann't allocate memory for filename: %s\n", filename);
		goto free_req;
	}

	return req;

free_req:
	rte_free(req);

	return NULL;
}

static void
sto_fstat_init_cb(struct sto_fstat_req *req, fstat_done_t fstat_done, void *priv)
{
	req->fstat_done = fstat_done;
	req->priv = priv;
}

void
sto_fstat_free(struct sto_fstat_req *req)
{
	free((char *) req->filename);
	rte_free(req);
}

struct sto_fstat_result {
	int returncode;
	uint64_t dev;
	uint64_t ino;
	uint32_t mode;
	uint64_t nlink;
	uint64_t uid;
	uint64_t gid;
	uint64_t rdev;
	uint64_t size;
	uint64_t blksize;
	uint64_t blocks;
	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
};

static void *
sto_fstat_result_alloc(void)
{
	return calloc(1, sizeof(struct sto_fstat_result));
}

static void
sto_fstat_result_free(void *arg)
{
	struct sto_fstat_result *result = arg;
	free(result);
}

static const struct spdk_json_object_decoder sto_fstat_result_decoders[] = {
	{"returncode", offsetof(struct sto_fstat_result, returncode), spdk_json_decode_int32},
	{"dev", offsetof(struct sto_fstat_result, dev), spdk_json_decode_uint64},
	{"ino", offsetof(struct sto_fstat_result, ino), spdk_json_decode_uint64},
	{"mode", offsetof(struct sto_fstat_result, mode), spdk_json_decode_uint32},
	{"nlink", offsetof(struct sto_fstat_result, nlink), spdk_json_decode_uint64},
	{"uid", offsetof(struct sto_fstat_result, uid), spdk_json_decode_uint64},
	{"gid", offsetof(struct sto_fstat_result, gid), spdk_json_decode_uint64},
	{"rdev", offsetof(struct sto_fstat_result, rdev), spdk_json_decode_uint64},
	{"size", offsetof(struct sto_fstat_result, size), spdk_json_decode_uint64},
	{"blksize", offsetof(struct sto_fstat_result, blksize), spdk_json_decode_uint64},
	{"blocks", offsetof(struct sto_fstat_result, blocks), spdk_json_decode_uint64},
	{"atime", offsetof(struct sto_fstat_result, atime), spdk_json_decode_uint64},
	{"mtime", offsetof(struct sto_fstat_result, mtime), spdk_json_decode_uint64},
	{"ctime", offsetof(struct sto_fstat_result, ctime), spdk_json_decode_uint64},
};

static struct sto_decoder sto_fstat_result_decoder =
	STO_DECODER_INITIALIZER(sto_fstat_result_decoders,
				sto_fstat_result_alloc, sto_fstat_result_free);

static int
sto_fstat_parse_result(void *priv, void *params)
{
	struct sto_fstat_req *req = priv;
	struct sto_fstat_result *result = params;
	struct sto_stat *stat = req->stat;

	SPDK_NOTICELOG("GLEB: Get result from FSTAT response: returncode[%d]\n",
		       result->returncode);

	req->returncode = result->returncode;

	if (spdk_unlikely(req->returncode)) {
		goto out;
	}

	stat->dev = result->dev;
	stat->ino = result->ino;
	stat->mode = result->mode;
	stat->nlink = result->nlink;
	stat->uid = result->uid;
	stat->gid = result->gid;
	stat->rdev = result->rdev;
	stat->size = result->size;
	stat->blksize = result->blksize;
	stat->blocks = result->blocks;
	stat->atime = result->atime;
	stat->mtime = result->mtime;
	stat->ctime = result->ctime;

out:
	return 0;
}

static void
sto_fstat_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct sto_fstat_req *req = priv;

	if (spdk_unlikely(rc)) {
		req->returncode = rc;
		goto out;
	}

	rc = sto_decoder_parse(&sto_fstat_result_decoder, resp->result,
			       sto_fstat_parse_result, req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to parse fstat result, rc=%d\n", rc);
		req->returncode = rc;
	}

out:
	req->fstat_done(req);
}

static void
sto_fstat_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_fstat_req *req = priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filename", req->filename);

	spdk_json_write_object_end(w);
}

static int
sto_fstat_submit(struct sto_fstat_req *req)
{
	struct sto_client_args args = {
		.priv = req,
		.response_handler = sto_fstat_resp_handler,
	};

	return sto_client_send("fstat", req, sto_fstat_info_json, &args);
}

int
sto_fstat(const char *filename, struct sto_fstat_ctx *ctx)
{
	struct sto_fstat_req *req;
	int rc;

	req = sto_fstat_alloc(filename);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc memory for fstat req\n");
		return -ENOMEM;
	}

	sto_fstat_init_cb(req, ctx->fstat_done, ctx->priv);

	req->stat = ctx->stat;

	rc = sto_fstat_submit(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to submit fstat, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_fstat_free(req);

	return rc;
}
