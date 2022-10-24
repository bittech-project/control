#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "err.h"
#include "sto_subsystem.h"
#include "sto_lib.h"
#include "scst.h"
#include "scst_lib.h"

enum scst_ops {
	SCST_OP_DRIVER_INIT,
	SCST_OP_DRIVER_DEINIT,

	SCST_OP_HANDLER_LIST,

	SCST_OP_DEV_OPEN,
	SCST_OP_DEV_CLOSE,
	SCST_OP_DEV_RESYNC,
	SCST_OP_DEV_LIST,

	SCST_OP_DGRP_ADD,
	SCST_OP_DGRP_DEL,
	SCST_OP_DGRP_LIST,
	SCST_OP_DGRP_ADD_DEV,
	SCST_OP_DGRP_DEL_DEV,

	SCST_OP_TARGET_ADD,
	SCST_OP_TARGET_DEL,

	SCST_OP_TARGET_LIST,

	SCST_OP_COUNT,
};

static const struct scst_cdbops scst_op_table[] = {
	{
		.op.ops = SCST_OP_DRIVER_INIT,
		.op.name = "driver_init",
		.constructor = scst_driver_init_req_constructor,
		.decode_cdb = scst_driver_init_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DRIVER_DEINIT,
		.op.name = "driver_deinit",
		.constructor = scst_driver_deinit_req_constructor,
		.decode_cdb = scst_driver_deinit_decode_cdb,
	},
	{
		.op.ops = SCST_OP_HANDLER_LIST,
		.op.name = "handler_list",
		.constructor = scst_readdir_req_constructor,
		.decode_cdb = scst_handler_list_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DEV_OPEN,
		.op.name = "dev_open",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_dev_open_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DEV_CLOSE,
		.op.name = "dev_close",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_dev_close_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DEV_RESYNC,
		.op.name = "dev_resync",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_dev_resync_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DEV_LIST,
		.op.name = "dev_list",
		.constructor = scst_readdir_req_constructor,
		.decode_cdb = scst_dev_list_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DGRP_ADD,
		.op.name = "dgrp_add",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_dgrp_add_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DGRP_DEL,
		.op.name = "dgrp_del",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_dgrp_del_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DGRP_LIST,
		.op.name = "dgrp_list",
		.constructor = scst_readdir_req_constructor,
		.decode_cdb = scst_dgrp_list_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DGRP_ADD_DEV,
		.op.name = "dgrp_add_dev",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_dgrp_add_dev_decode_cdb,
	},
	{
		.op.ops = SCST_OP_DGRP_DEL_DEV,
		.op.name = "dgrp_del_dev",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_dgrp_del_dev_decode_cdb,
	},
	{
		.op.ops = SCST_OP_TARGET_ADD,
		.op.name = "target_add",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_target_add_decode_cdb,
	},
	{
		.op.ops = SCST_OP_TARGET_DEL,
		.op.name = "target_del",
		.constructor = scst_write_file_req_constructor,
		.decode_cdb = scst_target_del_decode_cdb,
	},
	{
		.op.ops = SCST_OP_TARGET_LIST,
		.op.name = "target_list",
		.constructor = scst_readdir_req_constructor,
		.decode_cdb = scst_target_list_decode_cdb,
	},
};

#define SCST_OP_TBL_SIZE	(SPDK_COUNTOF(scst_op_table))

static const struct scst_cdbops *
scst_find_cdbops(const char *op_name)
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
	int rc = 0;

	cdb = sto_decode_next_cdb(params);
	if (IS_ERR(cdb)) {
		SPDK_ERRLOG("SCST: Failed to decode CDB for req[%p]\n", req);
		return PTR_ERR(cdb);
	}

	rc = req->decode_cdb(req, cdb);
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

	req = op->constructor(op);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("SCST: Failed to construct req\n");
		return NULL;
	}

	rc = scst_decode_params(req, params);
	if (rc) {
		SPDK_ERRLOG("SCST: Failed to decode params, rc=%d\n", rc);
		req->free(req);
		return NULL;
	}

	return &req->ctx;
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
scst_end_response(struct sto_context *ctx, struct spdk_json_write_ctx *w)
{
	struct scst_req *req = to_scst_req(ctx);

	req->end_response(req, w);

	return;
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
	.end_response = scst_end_response,
	.free = scst_free,
};

STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
