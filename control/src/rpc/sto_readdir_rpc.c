#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_readdir_front.h"

struct sto_readdir_params {
	char *dirname;
};

static void
sto_readdir_params_free(struct sto_readdir_params *params)
{
	free(params->dirname);
}

static const struct spdk_json_object_decoder sto_readdir_decoders[] = {
	{"dirname", offsetof(struct sto_readdir_params, dirname), spdk_json_decode_string},
};

static void
sto_readdir_response(struct sto_readdir_ctx *ctx, struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;
	int i;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", ctx->returncode);

	spdk_json_write_named_array_begin(w, "dirents");

	for (i = 0; i < ctx->dirent_cnt; i++) {
		spdk_json_write_string(w, ctx->dirents[i]);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_named_int32(w, "dirent_cnt", ctx->dirent_cnt);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_readdir_done(struct sto_readdir_ctx *ctx)
{
	struct spdk_jsonrpc_request *request = ctx->priv;

	sto_readdir_response(ctx, request);

	sto_readdir_free(ctx);
}

static void
sto_rpc_readdir(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct sto_readdir_params rd_params = {};
	int rc;

	if (spdk_json_decode_object(params, sto_readdir_decoders,
				    SPDK_COUNTOF(sto_readdir_decoders), &rd_params)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		return;
	}

	rc = sto_readdir(rd_params.dirname, sto_readdir_done, request);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto out;
	}

out:
	sto_readdir_params_free(&rd_params);

	return;
}
SPDK_RPC_REGISTER("readdir", sto_rpc_readdir, SPDK_RPC_RUNTIME)
