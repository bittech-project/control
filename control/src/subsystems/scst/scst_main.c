#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/queue.h>
#include <spdk/likely.h>
#include <spdk/log.h>
#include <spdk/string.h>

#include "scst.h"

#include "sto_json.h"
#include "sto_pipeline.h"
#include "sto_err.h"
#include "sto_rpc_aio.h"

#define SCST_DEF_CONFIG_PATH "/etc/control.scst.json"

struct scst {
	const char *sys_path;
	const char *config_path;

	struct sto_pipeline_engine *engine;
};

static struct scst *g_scst;


static struct scst *
scst_create(void)
{
	struct scst *scst;

	scst = calloc(1, sizeof(*scst));
	if (spdk_unlikely(!scst)) {
		SPDK_ERRLOG("Failed to alloc SCST instance\n");
		return NULL;
	}

	scst->sys_path = SCST_ROOT;

	scst->config_path = strdup(SCST_DEF_CONFIG_PATH);
	if (spdk_unlikely(!scst->config_path)) {
		SPDK_ERRLOG("Failed to strdup config path for SCST\n");
		goto free_scst;
	}

	scst->engine = sto_pipeline_engine_create("SCST subsystem");
	if (spdk_unlikely(!scst->engine)) {
		SPDK_ERRLOG("Cann't create the SCST engine\n");
		goto free_config_path;
	}

	return scst;

free_config_path:
	free((char *) scst->config_path);

free_scst:
	free(scst);

	return NULL;
}

static void
scst_destroy(struct scst *scst)
{
	sto_pipeline_engine_destroy(scst->engine);
	free((char *) scst->config_path);
	free(scst);
}

void
scst_pipeline(const struct sto_pipeline_properties *properties,
	      sto_generic_cb cb_fn, void *cb_arg, void *priv)
{
	struct scst *scst = g_scst;

	sto_pipeline_alloc_and_run(scst->engine, properties, cb_fn, cb_arg, priv);
}

void
scst_init(sto_generic_cb cb_fn, void *cb_arg)
{
	struct scst *scst;
	int rc = 0;

	if (g_scst) {
		SPDK_ERRLOG("FAILED: SCST has already been initialized\n");
		rc = -EINVAL;
		goto out;
	}

	scst = scst_create();
	if (spdk_unlikely(!scst)) {
		SPDK_ERRLOG("Faild to create SCST\n");
		rc = -ENOMEM;
		goto out;
	}

	g_scst = scst;
out:
	cb_fn(cb_arg, rc);
}

void
scst_fini(sto_generic_cb cb_fn, void *cb_arg)
{
	struct scst *scst = g_scst;

	scst_destroy(scst);

	cb_fn(cb_arg, 0);
}
