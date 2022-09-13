#include <spdk/log.h>
#include <spdk_internal/init.h>
#include <spdk/json.h>

#include "sto_subsystem.h"
#include "sto_exec.h"

static spdk_sto_init_cb g_init_cb_fn = NULL;
static void *g_init_cb_arg = NULL;

static spdk_sto_fini_cb g_fini_cb_fn;
static void *g_fini_cb_arg;

static void
sto_init_complete(int rc)
{
	spdk_sto_init_cb cb_fn = g_init_cb_fn;
	void *cb_arg = g_init_cb_arg;

	g_init_cb_fn = NULL;
	g_init_cb_arg = NULL;

	cb_fn(cb_arg, rc);
}

void
spdk_sto_init(spdk_sto_init_cb cb_fn, void *cb_arg)
{
	int rc;

	assert(cb_fn != NULL);
	g_init_cb_fn = cb_fn;
	g_init_cb_arg = cb_arg;

	rc = sto_exec_init();
	if (rc < 0) {
		SPDK_ERRLOG("sto_exec_init() failed, rc=%d\n", rc);
		sto_init_complete(-1);
		return;
	}

	sto_init_complete(0);
}

static void
sto_fini_done(void)
{
	g_fini_cb_fn(g_fini_cb_arg);
}

void
spdk_sto_fini(spdk_sto_fini_cb cb_fn, void *cb_arg)
{
	g_fini_cb_fn = cb_fn;
	g_fini_cb_arg = cb_arg;

	sto_exec_exit();

	sto_fini_done();
}

void
spdk_sto_config_json(struct spdk_json_write_ctx *w)
{
}

SPDK_LOG_REGISTER_COMPONENT(sto_control)
