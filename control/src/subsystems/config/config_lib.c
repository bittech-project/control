#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_version.h"
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

struct sto_show_req_params {
	const char *subsystem;
	const char *op;
};

static void
sto_show_req_params_free(struct sto_show_req_params *params)
{
	free((char *) params->subsystem);
	free((char *) params->op);
}

static const struct spdk_json_object_decoder sto_show_req_decoders[] = {
	{"subsystem", offsetof(struct sto_show_req_params, subsystem), spdk_json_decode_string, true},
	{"op", offsetof(struct sto_show_req_params, op), spdk_json_decode_string, true},
};

struct sto_show_req {
	struct sto_req req;

	struct sto_show_req_params params;
};

static int
sto_show_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_show_req *show_req = STO_REQ_TYPE(req, show);
	struct sto_show_req_params *params = &show_req->params;
	int rc = 0;

	if (!cdb) {
		goto out;
	}

	if (spdk_json_decode_object(cdb, sto_show_req_decoders,
				    SPDK_COUNTOF(sto_show_req_decoders), params)) {
		SPDK_ERRLOG("Failed to decode show params\n");
		return -EINVAL;
	}

	SPDK_ERRLOG("GLEB: subsystem: %s, op: %s\n", params->subsystem, params->op);

	if (params->op && !params->subsystem) {
		SPDK_ERRLOG("Subsystem wasn't set\n");
		rc = -EINVAL;
		goto out_err;
	}

out:
	return 0;

out_err:
	sto_show_req_params_free(params);

	return rc;
}

static void
sto_show_req_free(struct sto_req *req)
{
	struct sto_show_req *show_req = STO_REQ_TYPE(req, show);

	sto_show_req_params_free(&show_req->params);

	rte_free(show_req);
}

static struct sto_req_ops sto_show_req_ops = {
	.decode_cdb = sto_show_req_decode_cdb,
	.exec = sto_dummy_req_exec,
	.end_response = sto_dummy_req_end_response,
	.free = sto_show_req_free,
};

static STO_REQ_CONSTRUCTOR_DEFINE(show)

static const struct sto_ops config_ops[] = {
	{
		.name = "version",
		.req_constructor = sto_dummy_req_constructor,
		.req_ops = &sto_version_req_ops,
	},
	{
		.name = "show",
		.req_constructor = sto_show_req_constructor,
		.req_ops = &sto_show_req_ops,
	},
};

static const struct sto_op_table config_op_table = STO_OP_TABLE_INITIALIZER(config_ops);

static struct sto_subsystem g_config_subsystem = STO_SUBSYSTEM_INITIALIZER("config", &config_op_table);
STO_SUBSYSTEM_REGISTER(g_config_subsystem);
