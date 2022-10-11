#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "scst.h"
#include "sto_core.h"
#include "sto_subsystem.h"

struct scst_exec_ctx {
	struct sto_subsystem *subsystem;
	void *subsys_req;

	struct spdk_jsonrpc_request *request;
};

static void
scst_response(struct sto_response *resp, struct spdk_jsonrpc_request *request)
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
scst_done(void *arg, struct sto_response *resp)
{
	struct scst_exec_ctx *ctx = arg;
	struct spdk_jsonrpc_request *request = ctx->request;
	struct sto_subsystem *subsystem;
	void *subsys_req;

	subsystem = ctx->subsystem;
	subsys_req = ctx->subsys_req;

	scst_response(resp, request);

	subsystem->done_req(subsys_req);

	free(ctx);
}

static void
scst(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct scst_exec_ctx *ctx;
	struct sto_subsystem *subsystem;
	void *subsys_req;
	int rc;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	ctx->request = request;

	subsystem = sto_subsystem_find("scst");
	if (spdk_unlikely(!subsystem)) {
		SPDK_ERRLOG("failed to find SCST subsystem\n");
		spdk_jsonrpc_send_error_response(request, -EINVAL, spdk_strerror(EINVAL));
		goto free_ctx;
	}

	ctx->subsystem = subsystem;

	subsys_req = subsystem->alloc_req(params);
	if (spdk_unlikely(!subsys_req)) {
		SPDK_ERRLOG("Failed to alloc SCST req\n");
		spdk_jsonrpc_send_error_response(request, -ENOMEM, spdk_strerror(ENOMEM));
		goto free_ctx;
	}

	ctx->subsys_req = subsys_req;

	subsystem->init_req(subsys_req, scst_done, ctx);

	rc = subsystem->exec_req(subsys_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to exec SCST req, rc=%d\n", rc);
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto free_req;
	}

	return;

free_req:
	subsystem->done_req(subsys_req);

free_ctx:
	free(ctx);

	return;
}
SPDK_RPC_REGISTER("scst", scst, SPDK_RPC_RUNTIME)
