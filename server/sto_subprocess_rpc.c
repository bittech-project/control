#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "sto_rpc.h"
#include "sto_srv_subprocess.h"

#define STO_SUBPROCESS_MAX_ARGS 256

struct sto_subprocess_arg_list {
	const char *args[STO_SUBPROCESS_MAX_ARGS + 1];
	size_t numargs;
};

static int
sto_subprocess_decode_cmd(const struct spdk_json_val *val, void *out)
{
	struct sto_subprocess_arg_list *arg_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, arg_list->args, STO_SUBPROCESS_MAX_ARGS,
				      &arg_list->numargs, sizeof(char *));
}

static void
sto_subprocess_free_cmd(struct sto_subprocess_arg_list *arg_list)
{
	ssize_t i;

	for (i = 0; i < arg_list->numargs; i++) {
		free((char *) arg_list->args[i]);
	}
}

struct sto_subprocess_params {
	struct sto_subprocess_arg_list arg_list;
	bool capture_output;
};

static void
sto_subprocess_free_params(struct sto_subprocess_params *params)
{
	sto_subprocess_free_cmd(&params->arg_list);
}

static const struct spdk_json_object_decoder sto_subprocess_decoders[] = {
	{"cmd", offsetof(struct sto_subprocess_params, arg_list), sto_subprocess_decode_cmd},
	{"capture_output", offsetof(struct sto_subprocess_params, capture_output), spdk_json_decode_bool, true},
};

struct sto_subprocess_req {
	struct spdk_jsonrpc_request *request;
	struct sto_subprocess_params params;
};

static void
sto_subprocess_free_req(struct sto_subprocess_req *req)
{
	sto_subprocess_free_params(&req->params);
	free(req);
}

static void
sto_srv_subprocess_rpc_done(void *priv, char *output, int rc)
{
	struct sto_subprocess_req *req = priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(req->request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", rc);
	spdk_json_write_named_string(w, "output", output);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(req->request, w);

	sto_subprocess_free_req(req);
}

static void
sto_srv_subprocess_rpc(struct spdk_jsonrpc_request *request,
		       const struct spdk_json_val *params)
{
	struct sto_subprocess_req *req;
	struct sto_subprocess_params *subp_params;
	struct sto_srv_subprocess_args args;
	int rc;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	subp_params = &req->params;

	if (spdk_json_decode_object(params, sto_subprocess_decoders,
				    SPDK_COUNTOF(sto_subprocess_decoders), subp_params)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	args.priv = req;
	args.done = sto_srv_subprocess_rpc_done;

	rc = sto_srv_subprocess(subp_params->arg_list.args, subp_params->arg_list.numargs, subp_params->capture_output,
				&args);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto free_params;
	}

	return;

free_params:
	sto_subprocess_free_params(&req->params);

free_req:
	free(req);

	return;
}
STO_RPC_REGISTER("subprocess", sto_srv_subprocess_rpc)
