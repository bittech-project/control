#include <spdk/string.h>
#include <spdk/likely.h>
#include <spdk/log.h>

#include "sto_module.h"
#include "sto_utils.h"
#include "sto_component.h"
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

static const struct sto_hash *
sto_module_get_ops(const char *object_name)
{
	struct sto_module *module;

	module = sto_module_find(object_name);
	if (spdk_unlikely(!module)) {
		SPDK_ERRLOG("Failed to find module %s\n", object_name);
		return ERR_PTR(-EINVAL);
	}

	return module->ops_map;
}

STO_CORE_REGISTER_COMPONENT(module, sto_module_get_ops)
