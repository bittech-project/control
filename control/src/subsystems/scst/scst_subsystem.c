#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "err.h"
#include "sto_subsystem.h"
#include "sto_core.h"
#include "scst.h"
#include "scst_lib.h"

enum scst_ops {
	SCST_OP_INIT,
	SCST_OP_DEINIT,
	SCST_OP_COUNT,
};

static const struct scst_cdbops scst_op_table[] = {
	{
		.op.ops = SCST_OP_INIT,
		.op.name = "init",
		.constructor = scst_req_init_constructor,
	},
	{
		.op.ops = SCST_OP_DEINIT,
		.op.name = "deinit",
		.constructor = scst_req_deinit_constructor,
	}
};

#define SCST_OP_TBL_SIZE	(SPDK_COUNTOF(scst_op_table))

static const struct scst_cdbops *
scst_get_cdbops(const char *op_name)
{
	int i;

	for (i = 0; i < SCST_OP_TBL_SIZE; i++) {
		const struct scst_cdbops *op = &scst_op_table[i];

		if (!strcmp(op_name, op->op.name)) {
			return op;
		}
	}

	return NULL;
}

static struct sto_context *
scst_parse(const struct spdk_json_val *params)
{
	const struct spdk_json_val *cdb;
	char *op_name = NULL;
	const struct scst_cdbops *op;
	struct scst_req *req = NULL;
	int rc;

	cdb = sto_decode_cdb(params, "op", &op_name);
	if (IS_ERR(cdb)) {
		int rc = PTR_ERR(cdb);
		SPDK_ERRLOG("SCST: Failed to decode CDB, rc=%d\n", rc);
		return NULL;
	}

	op = scst_get_cdbops(op_name);
	if (!op) {
		SPDK_ERRLOG("SCST: Failed to find op %s\n", op_name);
		goto out;
	}

	req = op->constructor(op);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("SCST: Failed to construct req\n");
		goto out;
	}

	rc = req->decode_cdb(req, cdb);
	if (rc) {
		SPDK_ERRLOG("SCST: Failed to decode CDB, rc=%d\n", rc);
		req->free(req);
		req = NULL;
	}
out:
	free(op_name);
	free((struct spdk_json_val *) cdb);

	return req ? &req->ctx : NULL;
}

static int
scst_exec(struct sto_context *ctx)
{
	struct scst_req *req = to_scst_req(ctx);
	int rc = 0;

	SPDK_NOTICELOG("SCST: Exec req[%p]\n", req);

	rc = scst_req_submit(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to submit SCST req, rc=%d\n", rc);
	}

	return rc;
}

static void
scst_free(struct sto_context *ctx)
{
	struct scst_req *req = to_scst_req(ctx);

	SPDK_NOTICELOG("SCST: Done req[%p]\n", req);

	req->free(req);

	return;
}

static struct sto_subsystem g_scst_subsystem = {
	.name = "scst",
	.init = scst_subsystem_init,
	.fini = scst_subsystem_fini,
	.parse = scst_parse,
	.exec = scst_exec,
	.free = scst_free,
};

STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
