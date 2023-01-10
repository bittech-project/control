#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_version.h"
#include "sto_module.h"
#include "sto_req.h"

static void
sto_version_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	spdk_json_write_string(w, STO_VERSION_STRING);
}

const struct sto_req_properties sto_version_req_properties = {
	.response = sto_version_req_response,
	.steps = {
		STO_REQ_STEP_TERMINATOR(),
	}
};

static const struct sto_ops config_ops[] = {
	{
		.name = "version",
		.req_properties = &sto_version_req_properties,
	}
};

static const struct sto_op_table config_op_table = STO_OP_TABLE_INITIALIZER(config_ops);

STO_MODULE_REGISTER(config, &config_op_table);
