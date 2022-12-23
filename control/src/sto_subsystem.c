#include <spdk/json.h>
#include <spdk/string.h>
#include <spdk/likely.h>
#include <spdk/log.h>

#include "sto_subsystem.h"
#include "sto_utils.h"
#include "sto_component.h"
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

static const struct sto_op_table *
sto_subsystem_decode(const struct sto_json_iter *iter)
{
	struct sto_subsystem *subsystem;
	char *subsystem_name = NULL;
	int rc = 0;

	rc = sto_json_iter_decode_str(iter, "subsystem", &subsystem_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode subsystem for rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	subsystem = sto_subsystem_find(subsystem_name);

	free(subsystem_name);

	if (spdk_unlikely(!subsystem)) {
		SPDK_ERRLOG("Failed to find subsystem\n");
		return ERR_PTR(-EINVAL);
	}

	return subsystem->op_table;
}

STO_CORE_REGISTER_COMPONENT(subsystem, sto_subsystem_decode)
