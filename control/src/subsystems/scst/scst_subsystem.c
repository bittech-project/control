#include <spdk/log.h>

#include "scst.h"
#include "sto_subsystem.h"
#include "sto_core.h"

int
scst_parse_req(struct sto_req *sto_req)
{
	SPDK_NOTICELOG("SCST: Parse req[%p]\n", sto_req);

	sto_req_set_state(sto_req, STO_REQ_STATE_EXEC);
	sto_req_process(sto_req);

	return 0;
}

int
scst_exec_req(struct sto_req *sto_req)
{
	SPDK_NOTICELOG("SCST: Exec req[%p]\n", sto_req);

	sto_req_set_state(sto_req, STO_REQ_STATE_DONE);
	sto_req_process(sto_req);

	return 0;
}

void
scst_done_req(struct sto_req *sto_req)
{
	SPDK_NOTICELOG("SCST: Done req[%p]\n", sto_req);

	return;
}

static struct sto_subsystem g_scst_subsystem = {
	.name = "scst",
	.parse = scst_parse_req,
	.exec = scst_exec_req,
	.done = scst_done_req,
};

STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
