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

struct sto_readdir_rpc_ctx {
	struct spdk_jsonrpc_request *request;
	struct sto_dirents dirents;
};

static void
sto_readdir_rpc_ctx_free(struct sto_readdir_rpc_ctx *ctx)
{
	sto_dirents_free(&ctx->dirents);
	free(ctx);
}

static void
sto_readdir_done(struct sto_readdir_req *req)
{
	struct sto_readdir_rpc_ctx *ctx = req->priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(ctx->request);

	spdk_json_write_array_begin(w);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", req->returncode);

	spdk_json_write_object_end(w);

	sto_dirents_dump_json("dirents", NULL, &ctx->dirents, w);

	spdk_json_write_array_end(w);

	spdk_jsonrpc_end_result(ctx->request, w);


	sto_readdir_rpc_ctx_free(ctx);
	sto_readdir_free(req);
}

static void
sto_rpc_readdir(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct sto_readdir_rpc_ctx *rpc_ctx;
	struct sto_readdir_params rd_params = {};
	struct sto_readdir_ctx ctx = {};
	int rc;

	if (spdk_json_decode_object(params, sto_readdir_decoders,
				    SPDK_COUNTOF(sto_readdir_decoders), &rd_params)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		return;
	}

	rpc_ctx = calloc(1, sizeof(*rpc_ctx));
	if (spdk_unlikely(!rpc_ctx)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		goto out;
	}

	rpc_ctx->request = request;

	ctx.dirents = &rpc_ctx->dirents;
	ctx.priv = rpc_ctx;
	ctx.readdir_done = sto_readdir_done;

	rc = sto_readdir(rd_params.dirname, &ctx);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto free_rpc_ctx;
	}

out:
	sto_readdir_params_free(&rd_params);

	return;

free_rpc_ctx:
	free(rpc_ctx);

	goto out;
}
SPDK_RPC_REGISTER("readdir", sto_rpc_readdir, SPDK_RPC_RUNTIME)
