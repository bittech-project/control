#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_client.h"
#include "sto_readdir_front.h"

static struct sto_readdir_ctx *
sto_readdir_ctx_alloc(const char *dirname)
{
	struct sto_readdir_ctx *ctx;

	ctx = rte_zmalloc(NULL, sizeof(*ctx), 0);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Cann't allocate memory for STO readdir ctx\n");
		return NULL;
	}

	ctx->dirname = strdup(dirname);
	if (spdk_unlikely(!ctx->dirname)) {
		SPDK_ERRLOG("Cann't allocate memory for dirname: %s\n", dirname);
		goto free_ctx;
	}

	return ctx;

free_ctx:
	rte_free(ctx);

	return NULL;
}

static void
sto_readdir_init_cb(struct sto_readdir_ctx *ctx, readdir_done_t readdir_done, void *priv)
{
	ctx->readdir_done = readdir_done;
	ctx->priv = priv;
}

static void sto_readdir_dirents_free(struct sto_readdir_ctx *ctx);

void
sto_readdir_free(struct sto_readdir_ctx *ctx)
{
	sto_readdir_dirents_free(ctx);

	free((char *) ctx->dirname);
	rte_free(ctx);
}

#define STO_DIRENT_MAX_CNT 256

struct sto_dirent_list {
	const char *dirents[STO_DIRENT_MAX_CNT];
	size_t dirent_cnt;
};

static int
sto_readdir_decode_dirents(const struct spdk_json_val *val, void *out)
{
	struct sto_dirent_list *dirent_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, dirent_list->dirents, STO_DIRENT_MAX_CNT,
				      &dirent_list->dirent_cnt, sizeof(char *));
}

static void
sto_readdir_free_dirents(struct sto_dirent_list *dirent_list)
{
	ssize_t i;

	for (i = 0; i < dirent_list->dirent_cnt; i++) {
		free((char *) dirent_list->dirents[i]);
	}
}

struct sto_readdir_result {
	int returncode;
	struct sto_dirent_list dirent_list;
	int dirent_cnt;
};

static void
sto_readdir_result_free(struct sto_readdir_result *result)
{
	sto_readdir_free_dirents(&result->dirent_list);
}

static const struct spdk_json_object_decoder sto_readdir_result_decoders[] = {
	{"returncode", offsetof(struct sto_readdir_result, returncode), spdk_json_decode_int32},
	{"dirents", offsetof(struct sto_readdir_result, dirent_list), sto_readdir_decode_dirents},
	{"dirent_cnt", offsetof(struct sto_readdir_result, dirent_cnt), spdk_json_decode_int32},
};

static int
sto_readdir_dirents_copy(struct sto_readdir_ctx *ctx,
			 struct sto_readdir_result *result)
{
	struct sto_dirent_list *dirent_list;
	int i, j;

	ctx->dirents = calloc(result->dirent_cnt, sizeof(char *));
	if (spdk_unlikely(!ctx->dirents)) {
		SPDK_ERRLOG("Failed to alloc %d dirents\n", ctx->dirent_cnt);
		return -ENOMEM;
	}

	dirent_list = &result->dirent_list;

	for (i = 0; i < result->dirent_cnt; i++) {
		ctx->dirents[i] = strdup(dirent_list->dirents[i]);
		if (spdk_unlikely(!ctx->dirents[i])) {
			SPDK_ERRLOG("Failed to copy %d dirent\n", i);
			goto free_dirents;
		}

		SPDK_NOTICELOG("GLEB: Dirent %s\n", ctx->dirents[i]);
	}

	ctx->dirent_cnt = result->dirent_cnt;

	return 0;

free_dirents:
	for (j = 0; j < i; j++) {
		free(ctx->dirents[j]);
	}

	free(ctx->dirents);

	return -ENOMEM;
}

static void
sto_readdir_dirents_free(struct sto_readdir_ctx *ctx)
{
	int i;

	if (spdk_unlikely(!ctx->dirent_cnt)) {
		return;
	}

	for (i = 0; i < ctx->dirent_cnt; i++) {
		free(ctx->dirents[i]);
	}

	free(ctx->dirents);
}

static void
sto_readdir_resp_handler(struct sto_rpc_request *rpc_req,
			 struct spdk_jsonrpc_client_response *resp)
{
	struct sto_readdir_ctx *ctx = rpc_req->priv;
	struct sto_readdir_result result;
	int rc;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_readdir_result_decoders,
				    SPDK_COUNTOF(sto_readdir_result_decoders), &result)) {
		SPDK_ERRLOG("Failed to decode response for subprocess\n");
		goto out;
	}

	SPDK_NOTICELOG("GLEB: Get result from READDIR response: returncode[%d], dir_cnt %d\n",
		       result.returncode, result.dirent_cnt);

	ctx->returncode = result.returncode;

	if (spdk_unlikely(ctx->returncode)) {
		goto free_result;
	}

	rc = sto_readdir_dirents_copy(ctx, &result);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to copy dirents, rc=%d\n", rc);
		ctx->returncode = rc;
		goto free_result;
	}

free_result:
	sto_readdir_result_free(&result);

out:
	sto_rpc_req_free(rpc_req);

	ctx->readdir_done(ctx);
}

static void
sto_readdir_info_json(struct sto_rpc_request *rpc_req, struct spdk_json_write_ctx *w)
{
	struct sto_readdir_ctx *ctx = rpc_req->priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "dirname", ctx->dirname);

	spdk_json_write_object_end(w);
}

static int
sto_readdir_submit(struct sto_readdir_ctx *ctx)
{
	struct sto_rpc_request *rpc_req;
	int rc = 0;

	rpc_req = sto_rpc_req_alloc("readdir", sto_readdir_info_json, ctx);
	if (spdk_unlikely(!rpc_req)) {
		SPDK_ERRLOG("Failed to alloc readdir RPC req\n");
		return -ENOMEM;
	}

	sto_rpc_req_init_cb(rpc_req, sto_readdir_resp_handler);

	rc = sto_client_send(rpc_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to send RPC req, rc=%d\n", rc);
		sto_rpc_req_free(rpc_req);
	}

	return rc;
}

int
sto_readdir(const char *dirname, readdir_done_t readdir_done, void *priv)
{
	struct sto_readdir_ctx *ctx;
	int rc;

	ctx = sto_readdir_ctx_alloc(dirname);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc memory for readdir ctx\n");
		return -ENOMEM;
	}

	sto_readdir_init_cb(ctx, readdir_done, priv);

	rc = sto_readdir_submit(ctx);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to submit readdir, rc=%d\n", rc);
		goto free_ctx;
	}

	return 0;

free_ctx:
	sto_readdir_free(ctx);

	return rc;
}
