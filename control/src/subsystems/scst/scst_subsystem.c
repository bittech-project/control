#include <spdk/log.h>
#include <spdk/likely.h>

#include "scst.h"
#include "sto_subsystem.h"
#include "sto_core.h"

static const struct sto_cdbops scst_op_table[] = {
	{
		.ops = SCST_OP_INIT,
		.name = "init",
	},
	{
		.ops = SCST_OP_DEINIT,
		.name = "deinit",
	}
};

#define SCST_OP_TBL_SIZE	(SPDK_COUNTOF(scst_op_table))

static const struct sto_cdbops *
scst_get_cdbops(char *op_name)
{
	int i;

	for (i = 0; i < SCST_OP_TBL_SIZE; i++) {
		if (strcmp(op_name, scst_op_table[i].name))
			return &scst_op_table[i];
	}

	return NULL;
}

static int
__scst_parse_req(struct sto_req *sto_req)
{
	struct scst_req *req;
	const struct sto_cdbops *op = sto_req->cdbops;

	switch (op->ops) {
	case SCST_OP_INIT:
		req = scst_construct_req_alloc(0);
		break;
	case SCST_OP_DEINIT:
		req = scst_destruct_req_alloc();
		break;
	default:
		assert(0);
	};

	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to create %s SCST req\n", op->name);
		return -ENOMEM;
	}

	sto_req->subsys_priv = req;

	return 0;
}

int
scst_parse_req(struct sto_req *sto_req)
{
	int rc;

	SPDK_NOTICELOG("SCST: Parse req[%p]\n", sto_req);

	rc = __scst_parse_req(sto_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("SCST: Failed to parse STO req, rc=%d\n", rc);
		return rc;
	}

	sto_req_set_state(sto_req, STO_REQ_STATE_EXEC);
	sto_req_process(sto_req);

	return 0;
}

static void
scst_exec_done(struct scst_req *req)
{
	struct sto_req *sto_req = req->priv;

	SPDK_NOTICELOG("SCST: Exec done for req[%p]\n", sto_req);

	sto_req_set_state(sto_req, STO_REQ_STATE_DONE);
	sto_req_process(sto_req);
}

int
scst_exec_req(struct sto_req *sto_req)
{
	struct scst_req *req = sto_req->subsys_priv;
	int rc = 0;

	SPDK_NOTICELOG("SCST: Exec req[%p]\n", sto_req);

	scst_req_init_cb(req, scst_exec_done, sto_req);

	rc = scst_req_submit(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to submit SCST init req, rc=%d\n", rc);
		goto out_err;
	}

	return 0;

out_err:
	sto_req_set_state(sto_req, STO_REQ_STATE_DONE);
	sto_req_process(sto_req);

	return 0;
}

void
scst_done_req(struct sto_req *sto_req)
{
	struct scst_req *req = sto_req->subsys_priv;

	SPDK_NOTICELOG("SCST: Done req[%p]\n", sto_req);

	scst_req_free(req);
	sto_req->subsys_priv = NULL;

	return;
}

static struct sto_subsystem g_scst_subsystem = {
	.name = "scst",
	.get_cdbops = scst_get_cdbops,
	.parse = scst_parse_req,
	.exec = scst_exec_req,
	.done = scst_done_req,
};

STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
