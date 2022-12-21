#include <spdk/string.h>
#include <spdk/likely.h>
#include <spdk/log.h>

#include "sto_module.h"
#include "sto_utils.h"
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

static const struct sto_op_table *
sto_module_decode(const struct sto_json_iter *iter)
{
	struct sto_module *module;
	char *module_name = NULL;
	int rc = 0;

	rc = sto_json_iter_decode_str(iter, "module", &module_name);
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

	return module->op_table;
}

static struct sto_core_component g_module_component =
	STO_CORE_COMPONENT_INITIALIZER("module", sto_module_decode);
STO_CORE_COMPONENT_REGISTER(g_module_component)
