#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_utils.h"
#include "sto_version.h"
#include "sto_module.h"
#include "sto_req.h"

static void
config_version_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	spdk_json_write_string(w, STO_VERSION_STRING);
}

const struct sto_req_properties config_version_req_properties = {
	.response = config_version_req_response,
	.steps = {
		STO_REQ_STEP_TERMINATOR(),
	}
};

enum config_info_type {
	CONFIG_INFO_TYPE_COMPONENT,
	CONFIG_INFO_TYPE_MODULE,
	CONFIG_INFO_TYPE_OP,
	CONFIG_INFO_TYPE_CNT
};

struct config_info_req_params {
	enum config_info_type type;

	union {
		const struct sto_module *module;
		const struct sto_ops *op;
	};
};

static void
config_modules_json_info(struct spdk_json_write_ctx *w)
{
	struct sto_module *module;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "modules");

	STO_FOREACH_MODULE(module) {
		spdk_json_write_string(w, module->name);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}

static void
config_module_ops_json_info(const struct sto_module *module, struct spdk_json_write_ctx *w)
{
	const struct sto_op_table *op_table;
	size_t i;

	op_table = module->op_table;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "ops");

	for (i = 0; i < op_table->size; i++) {
		spdk_json_write_string(w, op_table->ops[i].name);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}

static void
config_op_params_json_info(const struct sto_ops_params_properties *properties, struct spdk_json_write_ctx *w)
{
	size_t i;

	if (!properties) {
		return;
	}

	spdk_json_write_name(w, "params");

	spdk_json_write_object_begin(w);

	for (i = 0; i < properties->num_descriptors; i++) {
		const struct sto_ops_param_dsc *dsc = &properties->descriptors[i];

		spdk_json_write_named_string(w, dsc->name, dsc->description);
	}

	spdk_json_write_object_end(w);
}

static void
config_op_json_info(const struct sto_ops *op, struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "name", op->name);
	config_op_params_json_info(op->params_properties, w);

	spdk_json_write_object_end(w);
}

static void
config_info_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct config_info_req_params *params = req->type.params;

	switch (params->type) {
	case CONFIG_INFO_TYPE_COMPONENT:
		config_modules_json_info(w);
		break;
	case CONFIG_INFO_TYPE_MODULE:
		config_module_ops_json_info(params->module, w);
		break;
	case CONFIG_INFO_TYPE_OP:
		config_op_json_info(params->op, w);
		break;
	default: {
		struct sto_err_context err;

		sto_err(&err, -EINVAL);
		sto_status_failed(w, &err);
	}
	}
}

const struct sto_req_properties config_info_req_properties = {
	.params_size = sizeof(struct config_info_req_params),

	.response = config_info_req_response,
	.steps = {
		STO_REQ_STEP_TERMINATOR(),
	}
};

struct config_info_params {
	char *object;
};

static const struct sto_ops_param_dsc config_info_params_descriptors[] = {
	STO_OPS_PARAM_STR_OPTIONAL(object, struct config_info_params, "object desc"),
};

static const struct sto_ops_params_properties config_info_params_properties =
	STO_OPS_PARAMS_INITIALIZER_EMPTY(config_info_params_descriptors, struct config_info_params);

static int
config_info_constructor(void *arg1, const void *arg2)
{
	struct config_info_req_params *req_params = arg1;
	const struct config_info_params *ops_params = arg2;
	char *tmp_buf;
#define CONFIG_INFO_MAX_OBJECTS 2
	char *objects[CONFIG_INFO_MAX_OBJECTS] = {};
	const struct sto_module *module;
	int ret, rc = 0;

	if (!ops_params) {
		req_params->type = CONFIG_INFO_TYPE_COMPONENT;
		return 0;
	}

	tmp_buf = strdup(ops_params->object);
	if (spdk_unlikely(!tmp_buf)) {
		return -ENOMEM;
	}

	ret = sto_strsplit(tmp_buf, strlen(tmp_buf), objects, SPDK_COUNTOF(objects), '/');
	if (spdk_unlikely(ret <= 0)) {
		rc = -EINVAL;
		goto out;
	}

	module = sto_module_find(objects[0]);
	if (spdk_unlikely(!module)) {
		rc = -EINVAL;
		goto out;
	}

	switch (ret) {
	case 1:
		req_params->type = CONFIG_INFO_TYPE_MODULE;
		req_params->module = module;

		break;
	case 2: {
		const struct sto_ops *op;

		op = sto_ops_map_find(&module->ops_map, objects[1]);
		if (spdk_unlikely(!op)) {
			rc = -EINVAL;
			goto out;
		}

		req_params->type = CONFIG_INFO_TYPE_OP;
		req_params->op = op;

		break;
	}
	}

out:
	free(tmp_buf);

	return rc;
}

static const struct sto_ops config_ops[] = {
	{
		.name = "version",
		.req_properties = &config_version_req_properties,
	},
	{
		.name = "info",
		.params_properties = &config_info_params_properties,
		.req_properties = &config_info_req_properties,
		.req_params_constructor = config_info_constructor,
	},
};

static const struct sto_op_table config_op_table = STO_OP_TABLE_INITIALIZER(config_ops);

STO_MODULE_REGISTER(config, &config_op_table);
