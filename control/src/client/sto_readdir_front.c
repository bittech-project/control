#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_client.h"
#include "sto_readdir_front.h"

static struct sto_readdir_ctx *
sto_readdir_alloc(const char *dirname)
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

	ctx->skip_hidden = true;

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

void
sto_readdir_free(struct sto_readdir_ctx *ctx)
{
	sto_dirents_free(&ctx->dirents);

	free((char *) ctx->dirname);
	rte_free(ctx);
}

#define STO_DIRENT_MAX_CNT 256

struct sto_dirent_list {
	const char *dirents[STO_DIRENT_MAX_CNT];
	size_t cnt;
};

static int
sto_dirent_list_decode(const struct spdk_json_val *val, void *out)
{
	struct sto_dirent_list *dirent_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, dirent_list->dirents, STO_DIRENT_MAX_CNT,
				      &dirent_list->cnt, sizeof(char *));
}

static void
sto_dirent_list_free(struct sto_dirent_list *dirent_list)
{
	ssize_t i;

	for (i = 0; i < dirent_list->cnt; i++) {
		free((char *) dirent_list->dirents[i]);
	}
}

struct sto_readdir_result {
	int returncode;
	struct sto_dirent_list dirent_list;
};

static void
sto_readdir_result_free(struct sto_readdir_result *result)
{
	sto_dirent_list_free(&result->dirent_list);
}

static const struct spdk_json_object_decoder sto_readdir_result_decoders[] = {
	{"returncode", offsetof(struct sto_readdir_result, returncode), spdk_json_decode_int32},
	{"dirents", offsetof(struct sto_readdir_result, dirent_list), sto_dirent_list_decode},
};

static void
sto_readdir_resp_handler(struct sto_rpc_request *rpc_req,
			 struct spdk_jsonrpc_client_response *resp)
{
	struct sto_readdir_ctx *ctx = rpc_req->priv;
	struct sto_readdir_result result;
	struct sto_dirent_list *dirent_list;
	int rc;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_readdir_result_decoders,
				    SPDK_COUNTOF(sto_readdir_result_decoders), &result)) {
		SPDK_ERRLOG("Failed to decode response for subprocess\n");
		goto out;
	}

	dirent_list = &result.dirent_list;

	SPDK_NOTICELOG("GLEB: Get result from READDIR response: returncode[%d], dir_cnt %d\n",
		       result.returncode, (int) result.dirent_list.cnt);

	ctx->returncode = result.returncode;

	if (spdk_unlikely(ctx->returncode)) {
		goto free_result;
	}

	rc = sto_dirents_init(&ctx->dirents, dirent_list->dirents, dirent_list->cnt);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to copy dirents, rc=%d\n", rc);
		ctx->returncode = rc;
		goto free_result;
	}

	{
		struct sto_dirents *dirents = &ctx->dirents;
		int i;

		for (i = 0; i < dirents->cnt; i++) {
			SPDK_ERRLOG("GLEB: Dirent: %s\n", dirents->dirents[i]);
		}
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
	spdk_json_write_named_bool(w, "skip_hidden", ctx->skip_hidden);

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

	ctx = sto_readdir_alloc(dirname);
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

int
sto_dirents_init(struct sto_dirents *dirents, const char **dirent_list, int cnt)
{
	int i, j;

	dirents->dirents = calloc(cnt, sizeof(char *));
	if (spdk_unlikely(!dirents->dirents)) {
		SPDK_ERRLOG("Failed to alloc %d dirents\n", cnt);
		return -ENOMEM;
	}

	for (i = 0; i < cnt; i++) {
		dirents->dirents[i] = strdup(dirent_list[i]);
		if (spdk_unlikely(!dirents->dirents[i])) {
			SPDK_ERRLOG("Failed to copy %d dirent\n", i);
			goto free_dirents;
		}
	}

	dirents->cnt = cnt;

	return 0;

free_dirents:
	for (j = 0; j < i; j++) {
		free((char *) dirents->dirents[j]);
	}

	free(dirents->dirents);

	return -ENOMEM;
}

void
sto_dirents_free(struct sto_dirents *dirents)
{
	int i;

	if (spdk_unlikely(!dirents->cnt)) {
		return;
	}

	for (i = 0; i < dirents->cnt; i++) {
		free((char *) dirents->dirents[i]);
	}

	free(dirents->dirents);
}
