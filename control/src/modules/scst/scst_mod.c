#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_module.h"
#include "sto_utils.h"
#include "sto_generic_req.h"
#include "sto_rpc_subprocess.h"
#include "sto_err.h"

static int
iscsi_start_daemon(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = sto_req_step_done,
	};

	SPDK_ERRLOG("GLEB: Start iscsi-scstd\n");

	return sto_rpc_subprocess_fmt("iscsi-scstd -p 3260", &args);
}

static int
iscsi_stop_daemon(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = sto_req_step_done,
	};

	SPDK_ERRLOG("GLEB: Stop iscsi-scstd\n");

	return sto_rpc_subprocess_fmt("pkill iscsi-scstd", &args);
}

static int
iscsi_modprobe(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = sto_req_step_done,
	};

	SPDK_ERRLOG("GLEB: Modprobe iscsi-scst\n");

	return sto_rpc_subprocess_fmt("modprobe %s", &args, "iscsi-scst");
}

static int
iscsi_rmmod(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = sto_req_step_done,
	};
	SPDK_ERRLOG("GLEB: Rmmod iscsi-scst\n");

	return sto_rpc_subprocess_fmt("rmmod %s", &args, "iscsi-scst");
}

const struct sto_req_properties sto_iscsi_init_req_properties = {
	.response = sto_dummy_req_response,
	.steps = {
		STO_REQ_STEP(iscsi_modprobe, iscsi_rmmod),
		STO_REQ_STEP(iscsi_start_daemon, iscsi_stop_daemon),
		STO_REQ_STEP_TERMINATOR(),
	}
};

const struct sto_req_properties sto_iscsi_deinit_req_properties = {
	.response = sto_dummy_req_response,
	.steps = {
		STO_REQ_STEP(iscsi_stop_daemon, NULL),
		STO_REQ_STEP(iscsi_rmmod, NULL),
		STO_REQ_STEP_TERMINATOR(),
	}
};

static int
scst_snapshot_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "snapshot", iter);
}

static int
scst_handler_list_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "handler_list", iter);
}

static int
scst_driver_list_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "driver_list", iter);
}

static int
scst_dev_open_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dev_open", iter);
}

static int
scst_dev_close_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dev_close", iter);
}

static int
scst_dev_resync_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dev_resync", iter);
}

static int
scst_dev_list_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dev_list", iter);
}

static int
scst_dgrp_add_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dgrp_add", iter);
}

static int
scst_dgrp_del_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dgrp_del", iter);
}

static int
scst_dgrp_list_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dgrp_list", iter);
}

static int
scst_dgrp_add_dev_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dgrp_add_dev", iter);
}

static int
scst_dgrp_del_dev_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dgrp_del_dev", iter);
}

static int
scst_tgrp_add_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "tgrp_add", iter);
}

static int
scst_tgrp_del_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "tgrp_del", iter);
}

static int
scst_tgrp_list_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "tgrp_list", iter);
}

static int
scst_tgrp_add_tgt_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "tgrp_add_tgt", iter);
}

static int
scst_tgrp_del_tgt_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "tgrp_del_tgt", iter);
}

static int
scst_target_add_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "target_add", iter);
}

static int
scst_target_del_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "target_del", iter);
}

static int
scst_target_list_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "target_list", iter);
}

static int
scst_target_enable_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "target_enable", iter);
}

static int
scst_target_disable_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "target_disable", iter);
}

static int
scst_group_add_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "group_add", iter);
}

static int
scst_group_del_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "group_del", iter);
}

static int
scst_lun_add_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "lun_add", iter);
}

static int
scst_lun_del_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "lun_del", iter);
}

static int
scst_lun_replace_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "lun_replace", iter);
}

static int
scst_lun_clear_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "lun_clear", iter);
}

static const struct sto_ops scst_ops[] = {
	{
		.name = "snapshot",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_snapshot_constructor,
	},
	{
		.name = "handler_list",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_handler_list_constructor,
	},
	{
		.name = "driver_list",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_driver_list_constructor,
	},
	{
		.name = "dev_open",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dev_open_constructor,
	},
	{
		.name = "dev_close",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dev_close_constructor,
	},
	{
		.name = "dev_resync",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dev_resync_constructor,
	},
	{
		.name = "dev_list",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dev_list_constructor,
	},
	{
		.name = "dgrp_add",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dgrp_add_constructor,
	},
	{
		.name = "dgrp_del",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dgrp_del_constructor,
	},
	{
		.name = "dgrp_list",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dgrp_list_constructor,
	},
	{
		.name = "dgrp_add_dev",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dgrp_add_dev_constructor,
	},
	{
		.name = "dgrp_del_dev",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dgrp_del_dev_constructor,
	},
	{
		.name = "tgrp_add",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_tgrp_add_constructor,
	},
	{
		.name = "tgrp_del",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_tgrp_del_constructor,
	},
	{
		.name = "tgrp_list",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_tgrp_list_constructor,
	},
	{
		.name = "tgrp_add_tgt",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_tgrp_add_tgt_constructor,
	},
	{
		.name = "tgrp_del_tgt",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_tgrp_del_tgt_constructor,
	},
	{
		.name = "target_add",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_target_add_constructor,
	},
	{
		.name = "target_del",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_target_del_constructor,
	},
	{
		.name = "target_list",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_target_list_constructor,
	},
	{
		.name = "target_enable",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_target_enable_constructor,
	},
	{
		.name = "target_disable",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_target_disable_constructor,
	},
	{
		.name = "group_add",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_group_add_constructor,
	},
	{
		.name = "group_del",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_group_del_constructor,
	},
	{
		.name = "lun_add",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_lun_add_constructor,
	},
	{
		.name = "lun_del",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_lun_del_constructor,
	},
	{
		.name = "lun_replace",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_lun_replace_constructor,
	},
	{
		.name = "lun_clear",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_lun_clear_constructor,
	},
	{
		.name = "iscsi_init",
		.req_properties = &sto_iscsi_init_req_properties,
	},
	{
		.name = "iscsi_deinit",
		.req_properties = &sto_iscsi_deinit_req_properties,
	},
};

static const struct sto_op_table scst_op_table = STO_OP_TABLE_INITIALIZER(scst_ops);

STO_MODULE_REGISTER(scst, &scst_op_table);
