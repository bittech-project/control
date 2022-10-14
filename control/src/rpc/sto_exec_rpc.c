#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_subprocess_front.h"

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

static void
sto_exec_response(struct sto_subprocess *subp, struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_subprocess_done(struct sto_subprocess *subp)
{
	struct sto_exec_req *req = subp->priv;

	sto_exec_response(subp, req->request);

	sto_subprocess_free(subp);

	sto_exec_free_req(req);
}

static void
sto_exec(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct sto_exec_req *req;
	struct sto_exec_params *exec_params;
	struct sto_subprocess *subp;
	int rc;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	exec_params = &req->params;

	if (spdk_json_decode_object(params, sto_exec_decoders,
				    SPDK_COUNTOF(sto_exec_decoders), exec_params)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	subp = sto_subprocess_alloc(exec_params->arg_list.args, exec_params->arg_list.numargs,
				    exec_params->capture_output);
	if (spdk_unlikely(!subp)) {
		spdk_jsonrpc_send_error_response(request, -ENOMEM, strerror(ENOMEM));
		goto free_params;
	}

	sto_subprocess_init_cb(subp, sto_subprocess_done, req);

	rc = sto_subprocess_run(subp);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto free_subp;
	}

	return;

free_subp:
	sto_subprocess_free(subp);

free_params:
	sto_exec_free_params(&req->params);

free_req:
	free(req);

	return;
}
SPDK_RPC_REGISTER("exec", sto_exec, SPDK_RPC_RUNTIME)
