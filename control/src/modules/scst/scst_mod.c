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
scst_dev_open_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	return sto_passthrough_req_params_set_subsystem(req_params, "scst", "dev_open", iter);
}

static const struct sto_ops scst_ops[] = {
	{
		.name = "dev_open",
		.req_properties = &sto_passthrough_req_properties,
		.req_params_constructor = scst_dev_open_constructor,
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
