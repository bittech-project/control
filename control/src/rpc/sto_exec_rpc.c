#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_client.h"

#define STO_EXEC_MAX_ARGS 256

struct sto_exec_arg_list {
	const char *args[STO_EXEC_MAX_ARGS + 1];
	size_t numargs;
};

static int
sto_exec_decode_cmd(const struct spdk_json_val *val, void *out)
{
	struct sto_exec_arg_list *arg_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, arg_list->args, STO_EXEC_MAX_ARGS,
				      &arg_list->numargs, sizeof(char *));
}

static void
sto_exec_free_cmd(struct sto_exec_arg_list *arg_list)
{
	ssize_t i;

	for (i = 0; i < arg_list->numargs; i++) {
		free((char *) arg_list->args[i]);
	}
}

struct sto_exec_params {
	struct sto_exec_arg_list arg_list;
	bool capture_output;
};

static void
sto_exec_free_params(struct sto_exec_params *params)
{
	sto_exec_free_cmd(&params->arg_list);
}

static const struct spdk_json_object_decoder sto_exec_decoders[] = {
	{"cmd", offsetof(struct sto_exec_params, arg_list), sto_exec_decode_cmd},
	{"capture_output", offsetof(struct sto_exec_params, capture_output), spdk_json_decode_bool},
};

struct sto_exec_req {
	struct spdk_jsonrpc_request *request;
	struct sto_exec_params params;
};

static void
sto_exec_free_req(struct sto_exec_req *req)
{
	sto_exec_free_params(&req->params);
	free(req);
}

struct sto_exec_result {
	int returncode;
	char *output;
};

static const struct spdk_json_object_decoder sto_exec_result_decoders[] = {
	{"returncode", offsetof(struct sto_exec_result, returncode), spdk_json_decode_int32},
	{"output", offsetof(struct sto_exec_result, output), spdk_json_decode_string},
};

static void
sto_exec_response(struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
__resp_handler(struct sto_rpc_request *rpc_req, struct spdk_jsonrpc_client_response *resp)
{
	struct sto_exec_req *req = rpc_req->priv;
	struct sto_exec_result result;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_exec_result_decoders,
				    SPDK_COUNTOF(sto_exec_result_decoders), &result)) {
		spdk_jsonrpc_send_error_response(req->request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto out;
	}

	SPDK_NOTICELOG("GLEB: EXEC: Get result from response: returncode[%d] output: %s\n",
		       result.returncode, result.output);

	sto_exec_response(req->request);

out:
	sto_rpc_req_free(rpc_req);

	sto_exec_free_req(req);
}

static void
sto_exec_info_json(struct sto_rpc_request *rpc_req, struct spdk_json_write_ctx *w)
{
	struct sto_exec_req *req = rpc_req->priv;
	struct sto_exec_params *params = &req->params;
	int i;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "cmd");
	for (i = 0; i < params->arg_list.numargs; i++) {
		spdk_json_write_string(w, params->arg_list.args[i]);
	}
	spdk_json_write_array_end(w);

	spdk_json_write_named_bool(w, "capture_output", params->capture_output);

	spdk_json_write_object_end(w);
}

static void
sto_rpc_exec(struct spdk_jsonrpc_request *request,
	     const struct spdk_json_val *params)
{
	struct sto_exec_req *req;
	struct sto_exec_params *exec_params;
	struct sto_rpc_request *rpc_req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	exec_params = &req->params;

	if (spdk_json_decode_object(params, sto_exec_decoders,
				    SPDK_COUNTOF(sto_exec_decoders), exec_params)) {
		SPDK_DEBUGLOG(sto_control, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	rpc_req = sto_rpc_req_alloc("subprocess", sto_exec_info_json, req);
	assert(rpc_req);

	sto_rpc_req_init_cb(rpc_req, __resp_handler);

	sto_client_send(rpc_req);

	return;

free_req:
	sto_exec_free_req(req);

	return;
}
SPDK_RPC_REGISTER("exec", sto_rpc_exec, SPDK_RPC_RUNTIME)
