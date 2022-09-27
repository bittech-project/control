#include <spdk/likely.h>
#include <spdk/util.h>

#include "sto_server.h"

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

static void
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
	{"capture_output", offsetof(struct sto_rpc_construct_subprocess, capture_output), spdk_json_decode_bool, true},
};

struct sto_rpc_subprocess_ctx {
	struct sto_rpc_construct_subprocess req;

	struct spdk_jsonrpc_request *request;
};

static void
sto_rpc_free_subprocess_ctx(struct sto_rpc_subprocess_ctx *ctx)
{
	sto_rpc_free_construct_subprocess(&ctx->req);
	free(ctx);
}

static void
sto_rpc_subprocess_response(struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_rpc_subprocess(struct spdk_jsonrpc_request *request,
		   const struct spdk_json_val *params)
{
	struct sto_rpc_subprocess_ctx *ctx;
	struct sto_rpc_construct_subprocess *req;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	req = &ctx->req;

	if (spdk_json_decode_object(params, sto_rpc_construct_subprocess_decoders,
				    SPDK_COUNTOF(sto_rpc_construct_subprocess_decoders), req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_ctx;
	}

	ctx->request = request;

	sto_rpc_subprocess_response(ctx->request);

free_ctx:
	sto_rpc_free_subprocess_ctx(ctx);

	return;
}
STO_RPC_REGISTER("subprocess", sto_rpc_subprocess)
