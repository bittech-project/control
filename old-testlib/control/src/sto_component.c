#include "sto_component.h"

#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/queue.h>

#include "sto_json.h"
#include "sto_err.h"

TAILQ_HEAD(sto_component_list, sto_core_component);
static struct sto_component_list g_components = TAILQ_HEAD_INITIALIZER(g_components);

static struct sto_core_component *g_next_component;

static bool g_components_initialized = false;
static bool g_components_init_interrupted = false;

static sto_core_component_init_fn g_component_start_fn = NULL;
static void *g_component_start_arg = NULL;

static sto_core_component_fini_fn g_component_stop_fn = NULL;
static void *g_component_stop_arg = NULL;


void
sto_core_add_component(struct sto_core_component *component)
{
	TAILQ_INSERT_TAIL(&g_components, component, list);
}

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

struct sto_core_component *
sto_core_component_find(const char *name, bool skip_internal)
{
	return _core_component_find(&g_components, name, skip_internal);
}

static inline struct sto_core_component *
core_component_next(struct sto_core_component *component, struct sto_component_list *list)
{
	return !component ? TAILQ_FIRST(list) : TAILQ_NEXT(component, list);
}

static struct sto_core_component *
sto_core_component_next(struct sto_core_component *component)
{
	return core_component_next(component, &g_components);
}

void
sto_core_component_init_next(int rc)
{
	/* The initialization is interrupted by the sto_core_component_fini, so just return */
	if (g_components_init_interrupted) {
		return;
	}

	if (rc) {
		SPDK_ERRLOG("Init core component %s failed\n", g_next_component->name);
		g_component_start_fn(g_component_start_arg, rc);
		return;
	}

	g_next_component = sto_core_component_next(g_next_component);

	if (!g_next_component) {
		g_components_initialized = true;
		g_component_start_fn(g_component_start_arg, rc);
		return;
	}

	if (g_next_component->init) {
		g_next_component->init();
		return;
	}

	sto_core_component_init_next(rc);
}

void
sto_core_component_init(sto_core_component_init_fn cb_fn, void *cb_arg)
{
	g_component_start_fn = cb_fn;
	g_component_start_arg = cb_arg;

	sto_core_component_init_next(0);
}

void
sto_core_component_fini_next(void)
{
	if (!g_next_component) {
		/* If the initialized flag is false, then we've failed to initialize
		 * the very first component and no de-init is needed
		 */
		if (g_components_initialized) {
			g_next_component = TAILQ_LAST(&g_components, sto_component_list);
		}
	} else {
		if (g_components_initialized || g_components_init_interrupted) {
			g_next_component = TAILQ_PREV(g_next_component, sto_component_list, list);
		} else {
			g_components_init_interrupted = true;
		}
	}

	while (g_next_component) {
		if (g_next_component->fini) {
			g_next_component->fini();
			return;
		}

		g_next_component = TAILQ_PREV(g_next_component, sto_component_list, list);
	}

	g_component_stop_fn(g_component_stop_arg);

	return;
}

void
sto_core_component_fini(sto_core_component_fini_fn cb_fn, void *cb_arg)
{
	g_component_stop_fn = cb_fn;
	g_component_stop_arg = cb_arg;

	sto_core_component_fini_next();
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

const struct sto_shash *
sto_core_component_decode(const struct sto_json_iter *iter, bool internal_user)
{
	const struct sto_shash *ops_map;
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

	ops_map = component->get_ops_map(object_name);

	free(object_name);

	return ops_map;
}
