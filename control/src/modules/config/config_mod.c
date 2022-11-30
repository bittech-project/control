#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_version.h"
#include "sto_module.h"
#include "sto_lib.h"

static void
sto_version_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	spdk_json_write_string(w, STO_VERSION_STRING);
}

struct sto_req_ops sto_version_req_ops = {
	.decode_cdb = sto_dummy_req_decode_cdb,
	.exec = sto_dummy_req_exec,
	.end_response = sto_version_req_end_response,
	.free = sto_dummy_req_free,
};

static const struct sto_ops config_ops[] = {
	{
		.name = "version",
		.req_constructor = sto_dummy_req_constructor,
		.req_ops = &sto_version_req_ops,
	}
};

static const struct sto_op_table config_op_table = STO_OP_TABLE_INITIALIZER(config_ops);

static struct sto_module g_config_module = STO_MODULE_INITIALIZER("config", &config_op_table);
STO_MODULE_REGISTER(g_config_module);
