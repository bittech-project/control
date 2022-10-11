#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_core.h"

static void
sto_control_response(struct sto_response *resp, struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	if (spdk_unlikely(!resp)) {
		SPDK_ERRLOG("CRITICAL: Failed to get response\n");

		w = spdk_jsonrpc_begin_result(request);

		spdk_json_write_string(w, "ERROR: Response is NULL");

		spdk_jsonrpc_end_result(request, w);
		return;
	}

	w = spdk_jsonrpc_begin_result(request);

	sto_response_dump_json(resp, w);

	spdk_jsonrpc_end_result(request, w);

	sto_response_free(resp);
}

static void
sto_req_done(struct sto_req *req)
{
	struct spdk_jsonrpc_request *request = req->priv;

	sto_control_response(req->resp, request);

	sto_req_free(req);
}

static void
control(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct sto_req *req;
	int rc;

	req = sto_req_alloc(params);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to create STO req\n");
		spdk_jsonrpc_send_error_response(request, -ENOMEM, strerror(ENOMEM));
		return;
	}

	sto_req_init_cb(req, sto_req_done, request);

	rc = sto_req_submit(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to send STO req\n");
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto free_req;
	}

	return;

free_req:
	sto_req_free(req);

	return;
}
SPDK_RPC_REGISTER("control", control, SPDK_RPC_RUNTIME)
