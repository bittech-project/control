#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_core.h"

static void
sto_control_rpc_done(struct sto_core_req *core_req)
{
	struct spdk_jsonrpc_request *request = core_req->priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	sto_core_req_response(core_req, w);

	spdk_jsonrpc_end_result(request, w);

	sto_core_req_free(core_req);
}

static void
sto_control_rpc(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	int rc;

	rc = sto_core_process(params, sto_control_rpc_done, request);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to start core process\n");
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto out;
	}

out:
	return;
}
SPDK_RPC_REGISTER("control", sto_control_rpc, SPDK_RPC_RUNTIME)
