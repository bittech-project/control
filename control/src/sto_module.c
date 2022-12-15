#include <spdk/string.h>
#include <spdk/likely.h>
#include <spdk/log.h>

#include "sto_module.h"
#include "sto_core.h"
#include "sto_err.h"

static TAILQ_HEAD(sto_module_list, sto_module) g_sto_modules
	= TAILQ_HEAD_INITIALIZER(g_sto_modules);


void
sto_add_module(struct sto_module *module)
{
	TAILQ_INSERT_TAIL(&g_sto_modules, module, list);
}

static struct sto_module *
_module_find(struct sto_module_list *list, const char *name)
{
	struct sto_module *module;

	TAILQ_FOREACH(module, list, list) {
		if (!strcmp(name, module->name)) {
			return module;
		}
	}

	return NULL;
}

struct sto_module *
sto_module_find(const char *name)
{
	return _module_find(&g_sto_modules, name);
}

static const struct sto_ops *
sto_module_find_ops(struct sto_module *module, const char *op_name)
{
	const struct sto_op_table *op_table;
	int i;

	op_table = module->op_table;
	assert(op_table);

	for (i = 0; i < op_table->size; i++) {
		const struct sto_ops *op = &op_table->ops[i];

		if (!strcmp(op_name, op->name)) {
			return op;
		}
	}

	return NULL;
}

static struct sto_module *
sto_module_parse(const struct spdk_json_val *params)
{
	struct sto_module *module;
	char *module_name = NULL;
	int rc = 0;

	rc = sto_json_decode_object_str(params, "module", &module_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode module for rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	module = sto_module_find(module_name);

	free(module_name);

	if (spdk_unlikely(!module)) {
		SPDK_ERRLOG("Failed to find module\n");
		return ERR_PTR(-EINVAL);
	}

	return module;
}

static const struct sto_ops *
sto_module_parse_ops(struct sto_module *module, const struct spdk_json_val *params)
{
	char *op_name = NULL;
	const struct sto_ops *op;
	int rc = 0;

	rc = sto_json_decode_object_str(params, "op", &op_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode op, rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	op = sto_module_find_ops(module, op_name);
	if (!op) {
		SPDK_ERRLOG("Failed to find op %s\n", op_name);
		free(op_name);
		return ERR_PTR(-EINVAL);
	}

	free(op_name);

	return op;
}

static const struct sto_ops *
sto_module_decode_ops(const struct spdk_json_val *params,
		      const struct spdk_json_val **params_cdb)
{
	struct sto_module *module;
	const struct spdk_json_val *op_cdb;
	const struct sto_ops *op;

	if (*params_cdb) {
		SPDK_ERRLOG("Params CDB is already set\n");
		return ERR_PTR(-EINVAL);
	}

	module = sto_module_parse(params);
	if (IS_ERR(module)) {
		SPDK_ERRLOG("Failed to parse module\n");
		return ERR_CAST(module);
	}

	op_cdb = sto_json_next_object(params);
	if (IS_ERR_OR_NULL(op_cdb)) {
		SPDK_ERRLOG("Failed to decode next JSON object\n");
		return op_cdb ? ERR_CAST(op_cdb) : ERR_PTR(-EINVAL);
	}

	op = sto_module_parse_ops(module, op_cdb);

	*params_cdb = sto_json_next_object_and_free(op_cdb);
	if (IS_ERR(*params_cdb)) {
		SPDK_ERRLOG("Failed to decode next JSON object\n");
		return ERR_CAST(*params_cdb);
	}

	return op;
}

static struct sto_core_component g_module_component = STO_CORE_COMPONENT_INITIALIZER("module", sto_module_decode_ops);
STO_CORE_COMPONENT_REGISTER(g_module_component)
