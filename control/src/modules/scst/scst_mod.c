#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_rpc_subprocess.h"
#include "sto_subsystem.h"
#include "sto_module.h"
#include "sto_core.h"

static int
iscsi_start_daemon(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = sto_req_exec_done,
	};

	SPDK_ERRLOG("GLEB: Start iscsi-scstd\n");

	return sto_rpc_subprocess_fmt("iscsi-scstd -p 3260", &args);
}

static int
iscsi_stop_daemon(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = sto_req_exec_done,
	};

	SPDK_ERRLOG("GLEB: Stop iscsi-scstd\n");

	return sto_rpc_subprocess_fmt("pkill iscsi-scstd", &args);
}

static int
iscsi_modprobe(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = sto_req_exec_done,
	};

	SPDK_ERRLOG("GLEB: Modprobe iscsi-scst\n");

	return sto_rpc_subprocess_fmt("modprobe %s", &args, "iscsi-scst");
}

static int
iscsi_rmmod(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = sto_req_exec_done,
	};
	SPDK_ERRLOG("GLEB: Rmmod iscsi-scst\n");

	return sto_rpc_subprocess_fmt("rmmod %s", &args, "iscsi-scst");
}

static int
iscsi_init_constructor(struct sto_req *req, int state)
{
	switch (state) {
	case 0:
		const struct sto_exec_entry e[] = {
			{iscsi_modprobe, iscsi_rmmod},
			{iscsi_start_daemon, iscsi_stop_daemon},
		};

		return STO_REQ_ADD_EXEC_ENTRIES(req, e);
	default:
		return 0;
	}
}

struct sto_req_ops sto_iscsi_init_req_ops = {
	.decode_cdb = sto_dummy_req_decode_cdb,
	.exec_constructor = iscsi_init_constructor,
	.response = sto_dummy_req_response,
	.free = sto_dummy_req_free,
};

static int
iscsi_deinit_constructor(struct sto_req *req, int state)
{
	switch (state) {
	case 0:
		const struct sto_exec_entry e[] = {
			{iscsi_stop_daemon, NULL},
			{iscsi_rmmod, NULL},
		};

		return STO_REQ_ADD_EXEC_ENTRIES(req, e);
	default:
		return 0;
	}
}

struct sto_req_ops sto_iscsi_deinit_req_ops = {
	.decode_cdb = sto_dummy_req_decode_cdb,
	.exec_constructor = iscsi_deinit_constructor,
	.response = sto_dummy_req_response,
	.free = sto_dummy_req_free,
};

static void
iscsi_add_target_done(void *priv, struct sto_core_req *core_req)
{
	struct sto_req *req = priv;
	int rc = core_req->err_ctx.rc;

	sto_core_req_free(core_req);

	sto_req_process(req, rc);
}

static void
iscsi_add_target_params(void *priv, struct spdk_json_write_ctx *w)
{
	spdk_json_write_named_string(w, "driver", "iscsi");
	spdk_json_write_named_string(w, "target", "gleb_iscsi");
}

static int
iscsi_add_target(struct sto_req *req)
{
	struct sto_subsystem_args args = {
		.priv = req,
		.done = iscsi_add_target_done,
	};

	return sto_subsystem_send("scst", "target_add", NULL, iscsi_add_target_params, &args);
}

static int
iscsi_add_target_constructor(struct sto_req *req, int state)
{
	switch (state) {
	case 0:
		return sto_req_add_exec(req, iscsi_add_target, NULL);
	default:
		return 0;
	}
}

struct sto_req_ops sto_iscsi_add_target_req_ops = {
	.decode_cdb = sto_dummy_req_decode_cdb,
	.exec_constructor = iscsi_add_target_constructor,
	.response = sto_dummy_req_response,
	.free = sto_dummy_req_free,
};

static const struct sto_ops scst_ops[] = {
	{
		.name = "iscsi_init",
		.req_constructor = sto_dummy_req_constructor,
		.req_ops = &sto_iscsi_init_req_ops,
	},
	{
		.name = "iscsi_add_target",
		.req_constructor = sto_dummy_req_constructor,
		.req_ops = &sto_iscsi_add_target_req_ops,
	},
	{
		.name = "iscsi_deinit",
		.req_constructor = sto_dummy_req_constructor,
		.req_ops = &sto_iscsi_deinit_req_ops,
	}
};

static const struct sto_op_table scst_op_table = STO_OP_TABLE_INITIALIZER(scst_ops);

static struct sto_module g_scst_module = STO_MODULE_INITIALIZER("scst", &scst_op_table);
STO_MODULE_REGISTER(g_scst_module);
