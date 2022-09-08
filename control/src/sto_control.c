#include <spdk_internal/init.h>

#include "sto_subsystem.h"

static void
sto_subsystem_init_complete(void *cb_arg, int rc)
{
	spdk_subsystem_init_next(rc);
}

static void
sto_subsystem_init(void)
{
	spdk_sto_init(sto_subsystem_init_complete, NULL);
}

static void
sto_subsystem_fini_done(void *arg)
{
	spdk_subsystem_fini_next();
}

static void
sto_subsystem_fini(void)
{
	spdk_sto_fini(sto_subsystem_fini_done, NULL);
}

static void
sto_subsystem_config_json(struct spdk_json_write_ctx *w)
{
	spdk_sto_config_json(w);
}

static struct spdk_subsystem g_spdk_subsystem_sto = {
	.name = "sto-control",
	.init = sto_subsystem_init,
	.fini = sto_subsystem_fini,
	.write_config_json = sto_subsystem_config_json,
};

SPDK_SUBSYSTEM_REGISTER(g_spdk_subsystem_sto);
