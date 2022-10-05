#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_scst.h"

struct sto_scst_params {
	unsigned long modules_bitmap;
};

static const struct spdk_json_object_decoder sto_scst_decoders[] = {
	{"modules", offsetof(struct sto_scst_params, modules_bitmap), spdk_json_decode_uint64},
};

struct sto_scst_req {
	struct spdk_jsonrpc_request *request;
	struct sto_scst_params params;
};

static void
sto_scst_free_req(struct sto_scst_req *req)
{
	free(req);
}

static void
sto_scst_response(struct scst_req *init_req, struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_scst_init_done(struct scst_req *init_req)
{
	struct sto_scst_req *req = init_req->priv;

	sto_scst_response(init_req, req->request);

	scst_req_free(init_req);

	sto_scst_free_req(req);
}

static void
sto_scst_init(struct spdk_jsonrpc_request *request,
	      const struct spdk_json_val *params)
{
	struct sto_scst_req *req;
	struct sto_scst_params *scst_params;
	struct scst_req *init_req;
	int rc;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	scst_params = &req->params;

	if (spdk_json_decode_object(params, sto_scst_decoders,
				    SPDK_COUNTOF(sto_scst_decoders), scst_params)) {
		SPDK_DEBUGLOG(sto_control, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	init_req = scst_construct_req_alloc(scst_params->modules_bitmap);
	if (spdk_unlikely(!init_req)) {
		SPDK_ERRLOG("Failed to create SCST init req\n");
		spdk_jsonrpc_send_error_response(request, -ENOMEM, strerror(ENOMEM));
		goto free_req;
	}

	scst_req_init_cb(init_req, sto_scst_init_done, req);

	rc = scst_req_submit(init_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run SCST init req\n");
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto free_init_req;
	}

	return;

free_init_req:
	scst_req_free(init_req);

free_req:
	sto_scst_free_req(req);

	return;
}
SPDK_RPC_REGISTER("scst_init", sto_scst_init, SPDK_RPC_RUNTIME)

static void
sto_scst_deinit_done(struct scst_req *deinit_req)
{
	struct sto_scst_req *req = deinit_req->priv;

	sto_scst_response(deinit_req, req->request);

	scst_req_free(deinit_req);

	sto_scst_free_req(req);
}

static void
sto_scst_deinit(struct spdk_jsonrpc_request *request,
		const struct spdk_json_val *params)
{
	struct sto_scst_req *req;
	struct scst_req *deinit_req;
	int rc;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	req->request = request;

	deinit_req = scst_destruct_req_alloc();
	if (spdk_unlikely(!deinit_req)) {
		SPDK_ERRLOG("Failed to create SCST init req\n");
		spdk_jsonrpc_send_error_response(request, -ENOMEM, strerror(ENOMEM));
		goto free_req;
	}

	scst_req_init_cb(deinit_req, sto_scst_deinit_done, req);

	rc = scst_req_submit(deinit_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run SCST init req\n");
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto free_deinit_req;
	}

	return;

free_deinit_req:
	scst_req_free(deinit_req);

free_req:
	sto_scst_free_req(req);

	return;
}
SPDK_RPC_REGISTER("scst_deinit", sto_scst_deinit, SPDK_RPC_RUNTIME)
