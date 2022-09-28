#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_client.h"

#define STO_EXEC_MAX_ARGS 256

struct sto_rpc_exec_arg_list {
	const char *args[STO_EXEC_MAX_ARGS + 1];
	size_t num_args;
};

static int
sto_rpc_decode_exec_cmd(const struct spdk_json_val *val, void *out)
{
	struct sto_rpc_exec_arg_list *arg_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, arg_list->args, STO_EXEC_MAX_ARGS,
				      &arg_list->num_args, sizeof(char *));
}

static void
sto_rpc_free_exec_cmd(struct sto_rpc_exec_arg_list *arg_list)
{
	ssize_t i;

	for (i = 0; i < arg_list->num_args; i++) {
		free((char *) arg_list->args[i]);
	}
}

struct sto_rpc_construct_exec {
	struct sto_rpc_exec_arg_list arg_list;
	bool capture_output;
};

static void
sto_rpc_free_construct_exec(struct sto_rpc_construct_exec *req)
{
	sto_rpc_free_exec_cmd(&req->arg_list);
}

static const struct spdk_json_object_decoder sto_rpc_construct_exec_decoders[] = {
	{"cmd", offsetof(struct sto_rpc_construct_exec, arg_list), sto_rpc_decode_exec_cmd},
	{"capture_output", offsetof(struct sto_rpc_construct_exec, capture_output), spdk_json_decode_bool},
};

struct sto_rpc_exec_ctx {
	struct sto_rpc_construct_exec req;

	struct spdk_jsonrpc_request *request;
};

static void
sto_rpc_free_exec_ctx(struct sto_rpc_exec_ctx *ctx)
{
	sto_rpc_free_construct_exec(&ctx->req);
	free(ctx);
}

struct sto_rpc_exec_result {
	int returncode;
	char *output;
};

static const struct spdk_json_object_decoder sto_rpc_exec_result_decoders[] = {
	{"returncode", offsetof(struct sto_rpc_exec_result, returncode), spdk_json_decode_int32},
	{"output", offsetof(struct sto_rpc_exec_result, output), spdk_json_decode_string},
};

static void
sto_rpc_exec_response(struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
__resp_handler(struct sto_rpc_request *req, struct spdk_jsonrpc_client_response *resp)
{
	struct sto_rpc_exec_ctx *ctx = req->priv;
	struct sto_rpc_exec_result result;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_rpc_exec_result_decoders,
				    SPDK_COUNTOF(sto_rpc_exec_result_decoders), &result)) {
		spdk_jsonrpc_send_error_response(ctx->request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto out;
	}

	SPDK_NOTICELOG("GLEB: EXEC: Get result from response: returncode[%d] output: %s\n",
		       result.returncode, result.output);

	sto_rpc_exec_response(ctx->request);

out:
	sto_rpc_req_free(req);

	sto_rpc_free_exec_ctx(ctx);
}

static void
sto_exec_info_json(struct sto_rpc_request *sto_req, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_exec_ctx *ctx = sto_req->priv;
	struct sto_rpc_construct_exec *req = &ctx->req;
	int i;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "cmd");
	for (i = 0; i < req->arg_list.num_args; i++) {
		spdk_json_write_string(w, req->arg_list.args[i]);
	}
	spdk_json_write_array_end(w);

	spdk_json_write_named_bool(w, "capture_output", req->capture_output);

	spdk_json_write_object_end(w);
}

static void
sto_rpc_exec(struct spdk_jsonrpc_request *request,
	     const struct spdk_json_val *params)
{
	struct sto_rpc_exec_ctx *ctx;
	struct sto_rpc_construct_exec *req;
	struct sto_rpc_request *sto_req;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	req = &ctx->req;

	if (spdk_json_decode_object(params, sto_rpc_construct_exec_decoders,
				    SPDK_COUNTOF(sto_rpc_construct_exec_decoders), req)) {
		SPDK_DEBUGLOG(sto_control, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_ctx;
	}

	ctx->request = request;

	sto_req = sto_rpc_req_alloc("subprocess", sto_exec_info_json, ctx);
	assert(sto_req);

	sto_rpc_req_init_cb(sto_req, __resp_handler);

	sto_client_send(sto_req);

	return;

free_ctx:
	sto_rpc_free_exec_ctx(ctx);

	return;
}
SPDK_RPC_REGISTER("exec", sto_rpc_exec, SPDK_RPC_RUNTIME)
