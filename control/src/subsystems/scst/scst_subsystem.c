#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>

#include "scst.h"
#include "sto_subsystem.h"
#include "sto_core.h"
#include "err.h"

static const struct scst_cdbops scst_op_table[] = {
	{
		.op.ops = SCST_OP_INIT,
		.op.name = "init",
		.constructor = scst_req_init_constructor,
		.fn = scst_constructor,
	},
	{
		.op.ops = SCST_OP_DEINIT,
		.op.name = "deinit",
		.constructor = scst_req_deinit_constructor,
		.fn = scst_destructor,
	}
};

#define SCST_OP_TBL_SIZE	(SPDK_COUNTOF(scst_op_table))

static const struct scst_cdbops *
scst_get_cdbops(const char *op_name)
{
	int i;

	for (i = 0; i < SCST_OP_TBL_SIZE; i++) {
		const struct scst_cdbops *op = &scst_op_table[i];

		if (!strncmp(op_name, op->op.name, sizeof(op->op.name))) {
			return op;
		}
	}

	return NULL;
}

static void *
scst_alloc_req(const struct spdk_json_val *params)
{
	const struct spdk_json_val *cdb;
	char *op_name = NULL;
	const struct scst_cdbops *op;
	struct scst_req *req = NULL;

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

	req = op->constructor(op, cdb);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("SCST: Failed to construct req\n");
		goto out;
	}

out:
	free(op_name);
	free((struct spdk_json_val *) cdb);

	return (void *) req;
}

static void
scst_init_req(void *req_arg, scst_req_done_t scst_req_done, void *priv)
{
	struct scst_req *req = req_arg;

	req->req_done = scst_req_done;
	req->priv = priv;
}

static int
scst_exec_req(void *req_arg)
{
	struct scst_req *req = req_arg;
	int rc = 0;

	SPDK_NOTICELOG("SCST: Exec req[%p]\n", req);

	rc = scst_req_submit(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to submit SCST req, rc=%d\n", rc);
	}

	return rc;
}

static void
scst_done_req(void *req_arg)
{
	struct scst_req *req = req_arg;

	SPDK_NOTICELOG("SCST: Done req[%p]\n", req);

	req->req_free(req);

	return;
}

static struct sto_subsystem g_scst_subsystem = {
	.name = "scst",
	.alloc_req = scst_alloc_req,
	.init_req  = scst_init_req,
	.exec_req  = scst_exec_req,
	.done_req  = scst_done_req,
};

STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
