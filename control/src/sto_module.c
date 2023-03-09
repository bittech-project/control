#include "sto_module.h"

#include <spdk/stdinc.h>
#include <spdk/likely.h>
#include <spdk/log.h>
#include <spdk/queue.h>

#include "sto_lib.h"
#include "sto_component.h"
#include "sto_err.h"
#include "sto_hash.h"

TAILQ_HEAD(sto_module_list, sto_module);
static struct sto_module_list g_modules = TAILQ_HEAD_INITIALIZER(g_modules);

typedef void (*sto_module_init_fn)(void *cb_arg, int rc);
typedef void (*sto_module_fini_fn)(void *cb_arg);

static struct sto_module *g_next_module;

static bool g_modules_initialized = false;
static bool g_modules_init_interrupted = false;

static sto_module_init_fn g_module_start_fn = NULL;
static void *g_module_start_arg = NULL;

static sto_module_fini_fn g_module_stop_fn = NULL;
static void *g_module_stop_arg = NULL;


void
sto_add_module(struct sto_module *module)
{
	TAILQ_INSERT_TAIL(&g_modules, module, list);
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
	return _module_find(&g_modules, name);
}

static inline struct sto_module *
module_next(struct sto_module *module, struct sto_module_list *list)
{
	return !module ? TAILQ_FIRST(list) : TAILQ_NEXT(module, list);
}

struct sto_module *
sto_module_next(struct sto_module *module)
{
	return module_next(module, &g_modules);
}

static void
__module_init(struct sto_module *module)
{
	int rc = 0;

	rc = sto_ops_map_init(&module->ops_map, module->op_table);
	if (spdk_unlikely(rc)) {
		goto next;
	}

	if (module->ops && module->ops->init) {
		module->ops->init();
		return;
	}

next:
	sto_module_init_next(rc);
}

void
sto_module_init_next(int rc)
{
	/* The initialization is interrupted by the module_fini, so just return */
	if (g_modules_init_interrupted) {
		return;
	}

	if (rc) {
		SPDK_ERRLOG("Init module %s failed\n", g_next_module->name);
		g_module_start_fn(g_module_start_arg, rc);
		return;
	}

	g_next_module = sto_module_next(g_next_module);

	if (!g_next_module) {
		g_modules_initialized = true;
		g_module_start_fn(g_module_start_arg, rc);
		return;
	}

	__module_init(g_next_module);
}

void
sto_module_fini_next(void)
{
	if (!g_next_module) {
		/* If the initialized flag is false, then we've failed to initialize
		 * the very first module and no de-init is needed
		 */
		if (g_modules_initialized) {
			g_next_module = TAILQ_LAST(&g_modules, sto_module_list);
		}
	} else {
		if (g_modules_initialized || g_modules_init_interrupted) {
			g_next_module = TAILQ_PREV(g_next_module, sto_module_list, list);
		} else {
			g_modules_init_interrupted = true;
		}
	}

	while (g_next_module) {
		sto_shash_destroy((struct sto_shash *) &g_next_module->ops_map);

		if (g_next_module->ops && g_next_module->ops->fini) {
			g_next_module->ops->fini();
			return;
		}

		g_next_module = TAILQ_PREV(g_next_module, sto_module_list, list);
	}

	g_module_stop_fn(g_module_stop_arg);

	return;
}

static const struct sto_shash *
module_get_ops_map(const char *object_name)
{
	struct sto_module *module;

	module = sto_module_find(object_name);
	if (spdk_unlikely(!module)) {
		SPDK_ERRLOG("Failed to find module %s\n", object_name);
		return ERR_PTR(-EINVAL);
	}

	return &module->ops_map;
}

static void
module_init_done(void *cb_arg, int rc)
{
	sto_core_component_init_next(rc);
}

static void
module_init(void)
{
	g_module_start_fn = module_init_done;
	g_module_start_arg = NULL;

	sto_module_init_next(0);
}

static void
module_fini_done(void *cb_arg)
{
	sto_core_component_fini_next();
}

static void
module_fini(void)
{
	g_module_stop_fn = module_fini_done;
	g_module_stop_arg = NULL;

	sto_module_fini_next();
}

static struct sto_core_component module = {
	.name = "module",
	.get_ops_map = module_get_ops_map,
	.init = module_init,
	.fini = module_fini,
};

STO_CORE_COMPONENT_REGISTER(module)
