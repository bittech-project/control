#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_core.h"

struct sto_core_params {
	const char *subsystem;
};

static const struct spdk_json_object_decoder sto_core_decoders[] = {
	{"subsystem", offsetof(struct sto_core_params, subsystem), spdk_json_decode_string},
};

struct sto_core_req {
	struct spdk_jsonrpc_request *request;
	struct sto_core_params params;
};

static void
sto_core_free_req(struct sto_core_req *req)
{
	free(req);
}

static void
sto_core_response(struct sto_req *sto_req, struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_send_done(struct sto_req *sto_req)
{
	struct sto_core_req *req = sto_req->priv;

	sto_core_response(sto_req, req->request);

	sto_req_free(sto_req);

	sto_core_free_req(req);
}

static void
sto_send(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct sto_core_req *req;
	struct sto_core_params *core_params;
	struct sto_req *sto_req;
	int rc;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	core_params = &req->params;

	if (spdk_json_decode_object(params, sto_core_decoders,
				    SPDK_COUNTOF(sto_core_decoders), core_params)) {
		SPDK_DEBUGLOG(sto_control, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	sto_req = sto_req_alloc(core_params->subsystem);
	if (spdk_unlikely(!sto_req)) {
		SPDK_ERRLOG("Failed to create STO req\n");
		spdk_jsonrpc_send_error_response(request, -ENOMEM, strerror(ENOMEM));
		goto free_req;
	}

	sto_req_init_cb(sto_req, sto_send_done, req);

	rc = sto_req_submit(sto_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to send STO req\n");
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto free_sto_req;
	}

	return;

free_sto_req:
	sto_req_free(sto_req);

free_req:
	sto_core_free_req(req);

	return;
}
SPDK_RPC_REGISTER("sto_send", sto_send, SPDK_RPC_RUNTIME)
