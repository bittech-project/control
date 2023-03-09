#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>

#include "sto_module.h"
#include "sto_generic_req.h"
#include "sto_rpc_subprocess.h"
#include "sto_lib.h"
#include "sto_pipeline.h"
#include "sto_req.h"

static void
iscsi_start_daemon(struct sto_pipeline *pipe)
{
	SPDK_ERRLOG("GLEB: Start iscsi-scstd\n");

	sto_rpc_subprocess_fmt("iscsi-scstd -p 3260", sto_pipeline_step_done, pipe, NULL);
}

static void
iscsi_stop_daemon(struct sto_pipeline *pipe)
{
	SPDK_ERRLOG("GLEB: Stop iscsi-scstd\n");

	sto_rpc_subprocess_fmt("pkill iscsi-scstd", sto_pipeline_step_done, pipe, NULL);
}

static void
iscsi_modprobe(struct sto_pipeline *pipe)
{
	SPDK_ERRLOG("GLEB: Modprobe iscsi-scst\n");

	sto_rpc_subprocess_fmt("modprobe %s", sto_pipeline_step_done, pipe, NULL, "iscsi-scst");
}

static void
iscsi_rmmod(struct sto_pipeline *pipe)
{
	SPDK_ERRLOG("GLEB: Rmmod iscsi-scst\n");

	sto_rpc_subprocess_fmt("rmmod %s", sto_pipeline_step_done, pipe, NULL, "iscsi-scst");
}

const struct sto_req_properties sto_iscsi_init_req_properties = {
	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(iscsi_modprobe, iscsi_rmmod),
		STO_PL_STEP(iscsi_start_daemon, iscsi_stop_daemon),
		STO_PL_STEP_TERMINATOR(),
	}
};

const struct sto_req_properties sto_iscsi_deinit_req_properties = {
	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(iscsi_stop_daemon, NULL),
		STO_PL_STEP(iscsi_rmmod, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

static void
scst_dev_open(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct sto_json_head_raw *head = sto_json_subsystem_head_raw("scst", "dev_open");
	int rc;

	STO_JSON_HEAD_RAW_ADD_SINGLE(head, STO_JSON_PARAM_RAW_STR("device", "gleb"));
	STO_JSON_HEAD_RAW_ADD_SINGLE(head, STO_JSON_PARAM_RAW_STR("handler", "vdisk_blockio"));
	STO_JSON_HEAD_RAW_ADD_SINGLE(head, STO_JSON_PARAM_RAW_STR("attributes", "filename=/dev/ram0"));

	rc = sto_req_core_submit(req, NULL, head);
	if (spdk_unlikely(rc)) {
		sto_pipeline_step_next(pipe, rc);
	}
}

const struct sto_req_properties scst_dev_over_iscsi_req_properties = {
	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(scst_dev_open, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

static const struct sto_ops scst_ops[] = {
	{
		.name = "snapshot",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "handler_list",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "driver_list",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dev_open",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dev_close",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dev_resync",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dev_list",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dgrp_add",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dgrp_del",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dgrp_list",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dgrp_add_dev",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "dgrp_del_dev",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "tgrp_add",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "tgrp_del",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "tgrp_list",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "tgrp_add_tgt",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "tgrp_del_tgt",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "target_add",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "target_del",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "target_list",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "target_enable",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "target_disable",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "group_add",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "group_del",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "lun_add",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "lun_del",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "lun_replace",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "lun_clear",
		.type = STO_OPS_TYPE_ALIAS,
		.component_name = "subsystem",
		.object_name = "scst",
	},
	{
		.name = "iscsi_init",
		.req_properties = &sto_iscsi_init_req_properties,
	},
	{
		.name = "iscsi_deinit",
		.req_properties = &sto_iscsi_deinit_req_properties,
	},
	{
		.name = "dev_over_iscsi",
		.req_properties = &scst_dev_over_iscsi_req_properties,
	},
};

static const struct sto_op_table scst_op_table = STO_OP_TABLE_INITIALIZER(scst_ops);

STO_MODULE_REGISTER(scst, &scst_op_table, NULL);
