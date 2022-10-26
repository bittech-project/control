#include <spdk/log.h>
#include <spdk/likely.h>

#include "scst.h"
#include "sto_subprocess_front.h"

static struct scst g_scst;

static const char *const scst_tgt_names[] = {
	[SCST_TGT_LOCAL]	= "scst_local",
	[SCST_TGT_FCST]		= "fcst",
	[SCST_TGT_ISCSI]	= "iscsi-scst",
	[SCST_TGT_ISER]		= "isert-scst",
	[SCST_TGT_IB]		= "ib_srpt",
	[SCST_TGT_QLA]		= "qla2x00tgt",
};

static const char *
scst_tgt_name(enum scst_tgt_type type)
{
	size_t index = type;

	if (spdk_unlikely(index >= SPDK_COUNTOF(scst_tgt_names))) {
		assert(0);
	}

	return scst_tgt_names[index];
}

struct scst_tgt *
scst_find_tgt_by_name(struct scst *scst, const char *name)
{
	int i;

	for (i = 0; i < SCST_TGT_COUNT; i++) {
		const char *tgt_name = scst->tgts[i].name;

		if (!strcmp(name, tgt_name)) {
			return &scst->tgts[i];
		}
	}

	return NULL;
}

static void
scst_tgt_init(struct scst_tgt *tgt, enum scst_tgt_type type)
{
	tgt->type = type;
	tgt->name = scst_tgt_name(type);
}

static const char *const scst_drv_names[] = {
	[SCST_DRV_CORE]		= "scst",
	[SCST_DRV_LOCAL]	= "scst_local",
	[SCST_DRV_FCST]		= "fcst",
	[SCST_DRV_ISCSI]	= "iscsi-scst",
	[SCST_DRV_ISER]		= "isert-scst",
	[SCST_DRV_IB]		= "ib_srpt",
	[SCST_DRV_QLA]		= "qla2xxx_scst",
	[SCST_DRV_QLA_TARGET]	= "qla2x00tgt",
	[SCST_DRV_TAPE]		= "scst_tape",
	[SCST_DRV_CDROM]	= "scst_cdrom",
	[SCST_DRV_CHANGER]	= "scst_changer",
	[SCST_DRV_DISK]		= "scst_disk",
	[SCST_DRV_MODISK]	= "scst_modisk",
	[SCST_DRV_PROCESSOR]	= "scst_processor",
	[SCST_DRV_RAID]		= "scst_raid",
	[SCST_DRV_USER]		= "scst_user",
	[SCST_DRV_VDISK]	= "scst_vdisk",
};

static const char *
scst_drv_name(enum scst_drv_type type)
{
	size_t index = type;

	if (spdk_unlikely(index >= SPDK_COUNTOF(scst_drv_names))) {
		assert(0);
	}

	return scst_drv_names[index];
}

struct scst_driver *
scst_find_drv_by_name(struct scst *scst, const char *name)
{
	int i;

	for (i = 0; i < SCST_DRV_COUNT; i++) {
		const char *drv_name = scst->drivers[i].name;

		if (!strcmp(name, drv_name)) {
			return &scst->drivers[i];
		}
	}

	return NULL;
}

static void
scst_drv_init(struct scst_driver *drv, enum scst_drv_type type)
{
	drv->type = type;
	drv->name = scst_drv_name(type);
	drv->status = DRV_UNLOADED;

	TAILQ_INIT(&drv->master_list);
	TAILQ_INIT(&drv->slave_list);
}

static void
scst_drv_bind(struct scst_driver *master, struct scst_driver *slave)
{
	struct scst_driver_dep *dep;

	dep = calloc(1, sizeof(*dep));
	assert(dep != NULL);

	dep->drv = master;
	TAILQ_INSERT_TAIL(&slave->master_list, dep, list);

	dep = calloc(1, sizeof(*dep));
	assert(dep != NULL);

	dep->drv = slave;
	TAILQ_INSERT_TAIL(&master->slave_list, dep, list);
}

static void
scst_drv_configure(struct scst *scst, struct scst_driver *drv)
{
	struct scst_driver *master_drv;

	if (drv->type == SCST_DRV_CORE) {
		return;
	}

	master_drv = &scst->drivers[SCST_DRV_CORE];

	scst_drv_bind(master_drv, drv);

	if (drv->type == SCST_DRV_ISER) {
		master_drv = &scst->drivers[SCST_DRV_ISCSI];

		scst_drv_bind(master_drv, drv);
		return;
	}

	if (drv->type == SCST_DRV_QLA_TARGET) {
		master_drv = &scst->drivers[SCST_DRV_QLA];

		scst_drv_bind(master_drv, drv);
		return;
	}

	return;
}

static void
scst_drv_fini(struct scst_driver *drv)
{
	struct scst_driver_dep *drv_dep, *tmp;

	TAILQ_FOREACH_SAFE(drv_dep, &drv->master_list, list, tmp) {
		TAILQ_REMOVE(&drv->master_list, drv_dep, list);
		free(drv_dep);
	}

	TAILQ_FOREACH_SAFE(drv_dep, &drv->slave_list, list, tmp) {
		TAILQ_REMOVE(&drv->slave_list, drv_dep, list);
		free(drv_dep);
	}
}

void
scst_subsystem_init(void)
{
	struct scst *scst = &g_scst;
	int i;

	if (spdk_unlikely(scst->initialized)) {
		SPDK_ERRLOG("SCST: Subsystem has already been initialized\n");
		return;
	}

	memset(scst, 0, sizeof(*scst));

	for (i = 0; i < SCST_DRV_COUNT; i++) {
		struct scst_driver *drv = &scst->drivers[i];

		scst_drv_init(drv, i);
		scst_drv_configure(scst, drv);
	}

	for (i = 0; i < SCST_TGT_COUNT; i++) {
		struct scst_tgt *tgt = &scst->tgts[i];

		scst_tgt_init(tgt, i);
	}

	scst->initialized = true;

	return;
}

void
scst_subsystem_fini(void)
{
	struct scst *scst = &g_scst;
	int i;

	if (spdk_unlikely(!scst->initialized)) {
		SPDK_ERRLOG("SCST: Subsystem has not been initialized yet\n");
		return;
	}

	for (i = 0; i < SCST_DRV_COUNT; i++) {
		struct scst_driver *drv = &scst->drivers[i];

		scst_drv_fini(drv);
	}

	scst->initialized = false;
}

void
scst_req_init(struct scst_req *req, const struct scst_cdbops *op)
{
	req->scst = &g_scst;
	req->op = op;
}
