#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_core.h"

static void
sto_core_req_done(struct sto_core_req *req)
{
	struct spdk_jsonrpc_request *request = req->priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	sto_core_req_end_response(req, w);

	spdk_jsonrpc_end_result(request, w);

	sto_core_req_free(req);
}

static void
control(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct sto_core_req *req;

	req = sto_core_req_alloc(params);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to create STO req\n");
		spdk_jsonrpc_send_error_response(request, -ENOMEM, strerror(ENOMEM));
		return;
	}

	sto_core_req_init_cb(req, sto_core_req_done, request);

	sto_core_req_submit(req);

	return;
}
SPDK_RPC_REGISTER("control", control, SPDK_RPC_RUNTIME)
