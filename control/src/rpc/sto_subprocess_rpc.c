#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_subprocess.h"

#define STO_SUBPROCESS_MAX_ARGS 256

struct sto_rpc_subprocess_arg_list {
	const char *args[STO_SUBPROCESS_MAX_ARGS + 1];
	size_t num_args;
};

static int
sto_rpc_decode_subprocess_cmd(const struct spdk_json_val *val, void *out)
{
	struct sto_rpc_subprocess_arg_list *arg_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, arg_list->args, STO_SUBPROCESS_MAX_ARGS,
				      &arg_list->num_args, sizeof(char *));
}

static int
sto_rpc_free_subprocess_cmd(struct sto_rpc_subprocess_arg_list *arg_list)
{
	ssize_t i;

	for (i = 0; i < arg_list->num_args; i++) {
		free((char *) arg_list->args[i]);
	}
}

struct sto_rpc_construct_subprocess {
	struct sto_rpc_subprocess_arg_list arg_list;
	bool capture_output;
};

static void
sto_rpc_free_construct_subprocess(struct sto_rpc_construct_subprocess *req)
{
	sto_rpc_free_subprocess_cmd(&req->arg_list);
}

static const struct spdk_json_object_decoder sto_rpc_construct_subprocess_decoders[] = {
	{"cmd", offsetof(struct sto_rpc_construct_subprocess, arg_list), sto_rpc_decode_subprocess_cmd},
	{"capture_output", offsetof(struct sto_rpc_construct_subprocess, capture_output), spdk_json_decode_bool},
};

struct sto_rpc_subprocess_ctx {
	struct sto_subprocess_ctx subp_ctx;
	struct sto_rpc_construct_subprocess req;

	struct spdk_jsonrpc_request *request;
};

static void
sto_rpc_free_subprocess_ctx(struct sto_rpc_subprocess_ctx *ctx)
{
	sto_rpc_free_construct_subprocess(&ctx->req);
	free(ctx);
}

#define to_subprocess_ctx(x) SPDK_CONTAINEROF(x, struct sto_rpc_subprocess_ctx, subp_ctx);

static void
sto_rpc_subprocess_finish(struct sto_subprocess_ctx *subp_ctx)
{
	struct sto_rpc_subprocess_ctx *ctx = to_subprocess_ctx(subp_ctx);
	struct spdk_json_write_ctx *w;

	SPDK_DEBUGLOG(sto_control, "RPC subprocess finish: rc=%d output=%s\n",
		      subp_ctx->returncode, subp_ctx->output);

	w = spdk_jsonrpc_begin_result(ctx->request);
	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", subp_ctx->returncode);
	spdk_json_write_named_string(w, "output", subp_ctx->output);

	spdk_json_write_object_end(w);
	spdk_jsonrpc_end_result(ctx->request, w);

	sto_rpc_free_subprocess_ctx(ctx);
}

static void
sto_rpc_subprocess(struct spdk_jsonrpc_request *request,
		   const struct spdk_json_val *params)
{
	struct sto_rpc_subprocess_ctx *ctx;
	struct sto_rpc_construct_subprocess *req;
	struct sto_subprocess *subp;
	int rc = 0;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	req = &ctx->req;

	if (spdk_json_decode_object(params, sto_rpc_construct_subprocess_decoders,
				    SPDK_COUNTOF(sto_rpc_construct_subprocess_decoders), req)) {
		SPDK_DEBUGLOG(sto_control, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_ctx;
	}

	ctx->request = request;
	ctx->subp_ctx.subprocess_done = sto_rpc_subprocess_finish;

	subp = sto_subprocess_create(req->arg_list.args, req->arg_list.num_args, req->capture_output, 0);
	if (spdk_unlikely(!subp)) {
		SPDK_ERRLOG("Failed to create subprocess\n");
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto free_ctx;
	}

	rc = sto_subprocess_run(subp, &ctx->subp_ctx);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run subprocess\n");
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto free_subp;
	}

	return;

free_subp:
	sto_subprocess_destroy(subp);

free_ctx:
	sto_rpc_free_subprocess_ctx(ctx);

	return;
}
SPDK_RPC_REGISTER("subprocess", sto_rpc_subprocess, SPDK_RPC_RUNTIME)
