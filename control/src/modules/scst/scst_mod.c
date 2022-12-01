#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_rpc_subprocess.h"
#include "sto_module.h"
#include "sto_lib.h"

/* modprobe iscsi-scst */

/* iscsi-scstd -p 3260 */

void
iscsi_modprobe_done(struct sto_subprocess *subp)
{
	struct sto_module_req *module_req = subp->priv;
	int rc = subp->returncode;

	sto_subprocess_free(subp);

	module_req->returncode = rc;

	sto_module_req_process(module_req);
}

int
iscsi_modprobe(struct sto_module_req *module_req)
{
	const char *const cmd[] = {
		"modprobe",
		"iscsi-scst",
	};

	SPDK_ERRLOG("GLEB: Modprobe iscsi-scst\n");

	return STO_SUBPROCESS_EXEC(cmd, iscsi_modprobe_done, module_req);
}

sto_module_transition_t iscsi_init_transitions[] = {
	iscsi_modprobe,
	iscsi_modprobe,
	iscsi_modprobe,
	iscsi_modprobe,
	iscsi_modprobe,
};

sto_module_tt iscsi_init_tt = STO_MODULE_TT_INITIALIZER(iscsi_init_transitions);

static struct sto_module_req_params_constructor iscsi_init_constructor = {
	.tt = &iscsi_init_tt,
};

static const struct sto_ops scst_ops[] = {
	{
		.name = "iscsi_init",
		.req_constructor = sto_module_req_constructor,
		.req_ops = &sto_module_req_ops,
		.params_constructor = &iscsi_init_constructor,
	}
};

static const struct sto_op_table scst_op_table = STO_OP_TABLE_INITIALIZER(scst_ops);

static struct sto_module g_scst_module = STO_MODULE_INITIALIZER("scst", &scst_op_table);
STO_MODULE_REGISTER(g_scst_module);
