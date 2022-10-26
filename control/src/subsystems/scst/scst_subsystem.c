#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "err.h"
#include "sto_subsystem.h"
#include "sto_lib.h"
#include "scst.h"
#include "scst_lib.h"

static const struct scst_cdbops *
scst_get_cdbops(const struct spdk_json_val *params)
{
	char *op_name = NULL;
	const struct scst_cdbops *op;
	int rc = 0;

	rc = sto_decode_object_str(params, "op", &op_name);
	if (rc) {
		SPDK_ERRLOG("SCST: Failed to decode op, rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	op = scst_find_cdbops(op_name);
	if (!op) {
		SPDK_ERRLOG("SCST: Failed to find op %s\n", op_name);
		free(op_name);
		return ERR_PTR(-EINVAL);
	}

	free(op_name);

	return op;
}

static int
scst_decode_params(struct scst_req *req, const struct spdk_json_val *params)
{
	const struct spdk_json_val *cdb;
	struct scst_req_ops *ops = scst_req_get_ops(req);
	int rc = 0;

	cdb = sto_decode_next_cdb(params);
	if (IS_ERR(cdb)) {
		SPDK_ERRLOG("SCST: Failed to decode CDB for req[%p]\n", req);
		return PTR_ERR(cdb);
	}

	rc = ops->decode_cdb(req, cdb);
	if (rc) {
		SPDK_ERRLOG("SCST: Failed to parse CDB for req[%p], rc=%d\n", req, rc);
		goto out;
	}

out:
	free((struct spdk_json_val *) cdb);

	return rc;
}

static struct sto_context *
scst_parse(const struct spdk_json_val *params)
{
	const struct scst_cdbops *op;
	struct scst_req *req = NULL;
	int rc;

	op = scst_get_cdbops(params);
	if (IS_ERR(op)) {
		SPDK_ERRLOG("SCST: Failed to decode params\n");
		return NULL;
	}

	req = op->req_constructor(op);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("SCST: Failed to construct req\n");
		return NULL;
	}

	rc = scst_decode_params(req, params);
	if (rc) {
		struct scst_req_ops *ops = scst_req_get_ops(req);
		SPDK_ERRLOG("SCST: Failed to decode params, rc=%d\n", rc);

		ops->free(req);
		return NULL;
	}

	return &req->ctx;
}

static int
scst_exec(struct sto_context *ctx)
{
	struct scst_req *req = to_scst_req(ctx);
	struct scst_req_ops *ops = scst_req_get_ops(req);

	SPDK_NOTICELOG("SCST: Exec req[%p]\n", req);

	return ops->exec(req);
}

static void
scst_end_response(struct sto_context *ctx, struct spdk_json_write_ctx *w)
{
	struct scst_req *req = to_scst_req(ctx);
	struct scst_req_ops *ops = scst_req_get_ops(req);

	SPDK_NOTICELOG("SCST: End response req[%p]\n", req);

	ops->end_response(req, w);

	return;
}

static void
scst_free(struct sto_context *ctx)
{
	struct scst_req *req = to_scst_req(ctx);
	struct scst_req_ops *ops = scst_req_get_ops(req);

	SPDK_NOTICELOG("SCST: Done req[%p]\n", req);

	ops->free(req);

	return;
}

static struct sto_subsystem g_scst_subsystem = {
	.name = "scst",
	.init = scst_subsystem_init,
	.fini = scst_subsystem_fini,
	.parse = scst_parse,
	.exec = scst_exec,
	.end_response = scst_end_response,
	.free = scst_free,
};

STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
