#include <spdk/log.h>
#include <spdk_internal/init.h>
#include <spdk/json.h>

#include "sto_control.h"
#include "sto_client.h"

static spdk_control_init_cb g_init_cb_fn = NULL;
static void *g_init_cb_arg = NULL;

static spdk_control_fini_cb g_fini_cb_fn;
static void *g_fini_cb_arg;

static void
sto_init_complete(int rc)
{
	spdk_control_init_cb cb_fn = g_init_cb_fn;
	void *cb_arg = g_init_cb_arg;

	g_init_cb_fn = NULL;
	g_init_cb_arg = NULL;

	cb_fn(cb_arg, rc);
}

void
spdk_control_init(spdk_control_init_cb cb_fn, void *cb_arg)
{
	int rc;

	assert(cb_fn != NULL);
	g_init_cb_fn = cb_fn;
	g_init_cb_arg = cb_arg;

	rc = sto_client_connect(STO_LOCAL_SERVER_ADDR, AF_UNIX);
	if (rc < 0) {
		SPDK_ERRLOG("sto_client_connect() failed, rc=%d\n", rc);
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
spdk_control_fini(spdk_control_fini_cb cb_fn, void *cb_arg)
{
	g_fini_cb_fn = cb_fn;
	g_fini_cb_arg = cb_arg;

	sto_client_close();

	sto_fini_done();
}

void
spdk_control_config_json(struct spdk_json_write_ctx *w)
{
}

SPDK_LOG_REGISTER_COMPONENT(sto_control)
