#include <spdk/log.h>
#include <spdk/likely.h>

#include "sto_component.h"
#include "sto_utils.h"
#include "sto_err.h"

static TAILQ_HEAD(sto_component_list, sto_core_component) g_sto_components =
	TAILQ_HEAD_INITIALIZER(g_sto_components);


static struct sto_core_component *
_core_component_find(struct sto_component_list *list, const char *name, bool skip_internal)
{
	struct sto_core_component *component;

	TAILQ_FOREACH(component, list, list) {
		if (skip_internal && component->internal) {
			continue;
		}

		if (!strcmp(name, component->name)) {
			return component;
		}
	}

	return NULL;
}

static struct sto_core_component *
_core_component_next(struct sto_core_component *component, struct sto_component_list *list)
{
	return !component ? TAILQ_FIRST(list) : TAILQ_NEXT(component, list);
}

struct sto_core_component *
sto_core_component_find(const char *name, bool skip_internal)
{
	return _core_component_find(&g_sto_components, name, skip_internal);
}

struct sto_core_component *
sto_core_component_next(struct sto_core_component *component)
{
	return _core_component_next(component, &g_sto_components);
}

void
sto_core_add_component(struct sto_core_component *component)
{
	TAILQ_INSERT_TAIL(&g_sto_components, component, list);
}

static const struct sto_core_component *
core_component_parse(const struct sto_json_iter *iter, bool internal_user)
{
	struct sto_core_component *component;
	char *component_name = NULL;
	int rc;

	rc = sto_json_iter_decode_name(iter, &component_name);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode component name, rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	component = sto_core_component_find(component_name, !internal_user);

	free(component_name);

	return component;
}

const struct sto_hash *
sto_core_component_decode(const struct sto_json_iter *iter, bool internal_user)
{
	const struct sto_hash *ops_map;
	const struct sto_core_component *component;
	char *object_name = NULL;
	int rc = 0;

	component = core_component_parse(iter, internal_user);
	if (spdk_unlikely(!component)) {
		SPDK_ERRLOG("Failed to find component\n");
		return ERR_PTR(-EINVAL);
	}

	rc = sto_json_iter_decode_str(iter, component->name, &object_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode subsystem for rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	ops_map = component->get_ops_fn(object_name);

	free(object_name);

	return ops_map;
}
