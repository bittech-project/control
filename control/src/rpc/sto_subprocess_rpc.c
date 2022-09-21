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
	{"capture_output", offsetof(struct sto_rpc_construct_subprocess, capture_output), spdk_json_decode_bool},
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
sto_rpc_subprocess_response(struct spdk_jsonrpc_request *request,
			    struct sto_subprocess *subp)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);
	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", subp->returncode);
	spdk_json_write_named_string(w, "output", subp->output);

	spdk_json_write_object_end(w);
	spdk_jsonrpc_end_result(request, w);
}

static void
sto_rpc_subprocess_done(struct sto_subprocess *subp)
{
	struct sto_rpc_subprocess_ctx *ctx = subp->priv;

	SPDK_DEBUGLOG(sto_control, "RPC subprocess finish: rc=%d output=%s\n",
		      subp->returncode, subp->output);

	sto_rpc_subprocess_response(ctx->request, subp);

	sto_subprocess_free(subp);

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

	subp = sto_subprocess_alloc(req->arg_list.args, req->arg_list.num_args, req->capture_output);
	if (spdk_unlikely(!subp)) {
		SPDK_ERRLOG("Failed to create subprocess\n");
		spdk_jsonrpc_send_error_response(request, -ENOMEM, spdk_strerror(ENOMEM));
		goto free_ctx;
	}

	sto_subprocess_init_cb(subp, sto_rpc_subprocess_done, ctx);

	rc = sto_subprocess_run(subp);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run subprocess\n");
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto free_subp;
	}

	return;

free_subp:
	sto_subprocess_free(subp);

free_ctx:
	sto_rpc_free_subprocess_ctx(ctx);

	return;
}
SPDK_RPC_REGISTER("subprocess", sto_rpc_subprocess, SPDK_RPC_RUNTIME)
