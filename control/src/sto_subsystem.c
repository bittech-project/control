#include <spdk/likely.h>
#include <spdk/log.h>

#include "sto_subsystem.h"
#include "sto_component.h"
#include "sto_json.h"
#include "sto_err.h"

static TAILQ_HEAD(sto_subsystem_list, sto_subsystem) g_sto_subsystems =
	TAILQ_HEAD_INITIALIZER(g_sto_subsystems);

void
sto_add_subsystem(struct sto_subsystem *subsystem)
{
	TAILQ_INSERT_TAIL(&g_sto_subsystems, subsystem, list);
}

static struct sto_subsystem *
_subsystem_find(struct sto_subsystem_list *list, const char *name)
{
	struct sto_subsystem *iter;

	TAILQ_FOREACH(iter, list, list) {
		if (!strcmp(name, iter->name)) {
			return iter;
		}
	}

	return NULL;
}

struct sto_subsystem *
sto_subsystem_find(const char *name)
{
	return _subsystem_find(&g_sto_subsystems, name);
}

static const struct sto_shash *
sto_subsystem_get_ops(const char *object_name)
{
	struct sto_subsystem *subsystem;

	subsystem = sto_subsystem_find(object_name);
	if (spdk_unlikely(!subsystem)) {
		SPDK_ERRLOG("Failed to find subsystem %s\n", object_name);
		return ERR_PTR(-EINVAL);
	}

	return &subsystem->ops_map;
}

STO_CORE_REGISTER_INTERNAL_COMPONENT(subsystem, sto_subsystem_get_ops)
