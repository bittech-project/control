#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_module.h"
#include "sto_core.h"
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

struct sto_passthrough_req_params {
	char *component;
	char *object;
	char *op;

	struct spdk_json_val *params;
};

static void
sto_passthrough_req_params_deinit(void *params_ptr)
{
	struct sto_passthrough_req_params *params = params_ptr;

	free(params->component);
	free(params->object);
	free(params->op);

	free((struct spdk_json_val *) params->params);
}

static void
sto_passthrough_req_done(struct sto_core_req *core_req)
{
	struct sto_req *req = core_req->priv;
	int rc = core_req->err_ctx.rc;

	sto_core_req_free(core_req);

	sto_req_step_next(req, rc);
}

static void
sto_passthrough_req_dump_params(void *priv, struct spdk_json_write_ctx *w)
{
	struct spdk_json_val *params = priv;
	struct spdk_json_val *it;

	if (!params) {
		return;
	}

	sto_json_print(params);

	for (it = spdk_json_object_first(params); it; it = spdk_json_next(it)) {
		spdk_json_write_val(w, it);
		spdk_json_write_val(w, it + 1);
	}
}

static int
sto_passthrough_req(struct sto_req *req)
{
	struct sto_passthrough_req_params *params = req->type.params;
	struct sto_core_args args = {
		.priv = req,
		.done = sto_passthrough_req_done,
	};

	return sto_core_process_component(params->component, params->object, params->op,
					  params->params, sto_passthrough_req_dump_params, &args);
}

const struct sto_req_properties sto_passthrough_req_properties = {
	.params_size = sizeof(struct sto_passthrough_req_params),
	.params_deinit = sto_passthrough_req_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_REQ_STEP(sto_passthrough_req, NULL),
		STO_REQ_STEP_TERMINATOR(),
	}
};

static int
scst_dev_open_constructor(void *arg1, const void *arg2)
{
	struct sto_passthrough_req_params *req_params = arg1;
	const struct sto_json_iter *iter = arg2;

	req_params->component = strdup("subsystem");
	if (spdk_unlikely(!req_params->component)) {
		return -ENOMEM;
	}

	req_params->object = strdup("scst");
	if (spdk_unlikely(!req_params->object)) {
		return -ENOMEM;
	}

	req_params->op = strdup("dev_open");
	if (spdk_unlikely(!req_params->op)) {
		return -ENOMEM;
	}

	req_params->params = (struct spdk_json_val *) sto_json_iter_cut_tail(iter);
	if (IS_ERR(req_params->params)) {
		return PTR_ERR(req_params->params);
	}

	return 0;
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
