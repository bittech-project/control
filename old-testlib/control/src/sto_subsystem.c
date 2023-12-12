#include "sto_subsystem.h"

#include <spdk/stdinc.h>
#include <spdk/likely.h>
#include <spdk/log.h>
#include <spdk/queue.h>

#include "sto_component.h"
#include "sto_lib.h"
#include "sto_err.h"
#include "sto_hash.h"

TAILQ_HEAD(sto_subsystem_list, sto_subsystem);
static struct sto_subsystem_list g_subsystems = TAILQ_HEAD_INITIALIZER(g_subsystems);

typedef void (*sto_subsystem_init_fn)(void *cb_arg, int rc);
typedef void (*sto_subsystem_fini_fn)(void *cb_arg);

static struct sto_subsystem *g_next_subsystem;

static bool g_subsystems_initialized = false;
static bool g_subsystems_init_interrupted = false;

static sto_subsystem_init_fn g_subsystem_start_fn = NULL;
static void *g_subsystem_start_arg = NULL;

static sto_subsystem_fini_fn g_subsystem_stop_fn = NULL;
static void *g_subsystem_stop_arg = NULL;


void
sto_add_subsystem(struct sto_subsystem *subsystem)
{
	TAILQ_INSERT_TAIL(&g_subsystems, subsystem, list);
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
	return _subsystem_find(&g_subsystems, name);
}

static inline struct sto_subsystem *
subsystem_next(struct sto_subsystem *subsystem, struct sto_subsystem_list *list)
{
	return !subsystem ? TAILQ_FIRST(list) : TAILQ_NEXT(subsystem, list);
}

static struct sto_subsystem *
sto_subsystem_next(struct sto_subsystem *subsystem)
{
	return subsystem_next(subsystem, &g_subsystems);
}

static void
__subsystem_init(struct sto_subsystem *subsystem)
{
	int rc = 0;

	rc = sto_ops_map_init(&subsystem->ops_map, subsystem->op_table);
	if (spdk_unlikely(rc)) {
		goto next;
	}

	if (subsystem->ops && subsystem->ops->init) {
		subsystem->ops->init();
		return;
	}

next:
	sto_subsystem_init_next(rc);
}

void
sto_subsystem_init_next(int rc)
{
	/* The initialization is interrupted by the subsystem_fini, so just return */
	if (g_subsystems_init_interrupted) {
		return;
	}

	if (rc) {
		SPDK_ERRLOG("Init subsystem %s failed\n", g_next_subsystem->name);
		g_subsystem_start_fn(g_subsystem_start_arg, rc);
		return;
	}

	g_next_subsystem = sto_subsystem_next(g_next_subsystem);

	if (!g_next_subsystem) {
		g_subsystems_initialized = true;
		g_subsystem_start_fn(g_subsystem_start_arg, rc);
		return;
	}

	__subsystem_init(g_next_subsystem);
}

void
sto_subsystem_fini_next(void)
{
	if (!g_next_subsystem) {
		/* If the initialized flag is false, then we've failed to initialize
		 * the very first subsystem and no de-init is needed
		 */
		if (g_subsystems_initialized) {
			g_next_subsystem = TAILQ_LAST(&g_subsystems, sto_subsystem_list);
		}
	} else {
		if (g_subsystems_initialized || g_subsystems_init_interrupted) {
			g_next_subsystem = TAILQ_PREV(g_next_subsystem, sto_subsystem_list, list);
		} else {
			g_subsystems_init_interrupted = true;
		}
	}

	while (g_next_subsystem) {
		sto_shash_destroy((struct sto_shash *) &g_next_subsystem->ops_map);

		if (g_next_subsystem->ops && g_next_subsystem->ops->fini) {
			g_next_subsystem->ops->fini();
			return;
		}

		g_next_subsystem = TAILQ_PREV(g_next_subsystem, sto_subsystem_list, list);
	}

	g_subsystem_stop_fn(g_subsystem_stop_arg);

	return;
}

static const struct sto_shash *
subsystem_get_ops_map(const char *object_name)
{
	struct sto_subsystem *subsystem;

	subsystem = sto_subsystem_find(object_name);
	if (spdk_unlikely(!subsystem)) {
		SPDK_ERRLOG("Failed to find subsystem %s\n", object_name);
		return ERR_PTR(-EINVAL);
	}

	return &subsystem->ops_map;
}

static void
subsystem_init_done(void *cb_arg, int rc)
{
	sto_core_component_init_next(rc);
}

static void
subsystem_init(void)
{
	g_subsystem_start_fn = subsystem_init_done;
	g_subsystem_start_arg = NULL;

	sto_subsystem_init_next(0);
}

static void
subsystem_fini_done(void *cb_arg)
{
	sto_core_component_fini_next();
}

static void
subsystem_fini(void)
{
	g_subsystem_stop_fn = subsystem_fini_done;
	g_subsystem_stop_arg = NULL;

	sto_subsystem_fini_next();
}

static struct sto_core_component subsystem = {
	.name = "subsystem",
	.get_ops_map = subsystem_get_ops_map,
	.init = subsystem_init,
	.fini = subsystem_fini,
	.internal = true,
};

STO_CORE_COMPONENT_REGISTER(subsystem)
