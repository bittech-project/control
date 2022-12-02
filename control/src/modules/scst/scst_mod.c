#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_rpc_subprocess.h"
#include "sto_module.h"
#include "sto_core.h"

/* modprobe iscsi-scst */

/* iscsi-scstd -p 3260 */

void
iscsi_modprobe_done(void *priv, int rc)
{
	struct sto_req *req = priv;

	req->returncode = rc;

	sto_req_exec_fini(req);
}

int
iscsi_modprobe(struct sto_req *req)
{
	struct sto_rpc_subprocess_args args = {
		.priv = req,
		.done = iscsi_modprobe_done,
	};
	const char *const cmd[] = {
		"modprobe",
		"iscsi-scst",
	};

	SPDK_ERRLOG("GLEB: Modprobe iscsi-scst\n");

	return STO_RPC_SUBPROCESS(cmd, &args);
}

struct sto_req_ops sto_iscsi_init_req_ops = {
	.decode_cdb = sto_dummy_req_decode_cdb,
	.exec = iscsi_modprobe,
	.end_response = sto_dummy_req_end_response,
	.free = sto_dummy_req_free,
};

static const struct sto_ops scst_ops[] = {
	{
		.name = "iscsi_init",
		.req_constructor = sto_dummy_req_constructor,
		.req_ops = &sto_iscsi_init_req_ops,
	}
};

static const struct sto_op_table scst_op_table = STO_OP_TABLE_INITIALIZER(scst_ops);

static struct sto_module g_scst_module = STO_MODULE_INITIALIZER("scst", &scst_op_table);
STO_MODULE_REGISTER(g_scst_module);
