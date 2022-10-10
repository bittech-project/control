#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>

#include <rte_malloc.h>

#include "scst.h"
#include "scst_lib.h"

struct scst_drv_name_list {
	const char *names[SCST_DRV_COUNT];
	size_t cnt;
};

static int
scst_drv_list_decode(const struct spdk_json_val *val, void *out)
{
	struct scst_drv_name_list *drv_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, drv_list->names, SCST_DRV_COUNT,
				      &drv_list->cnt, sizeof(char *));
}

static void
scst_drv_list_free(struct scst_drv_name_list *drv_list)
{
	ssize_t i;

	for (i = 0; i < drv_list->cnt; i++) {
		free((char *) drv_list->names[i]);
	}
}

struct scst_init_params {
	struct scst_drv_name_list drv_list;
};

static void
scst_init_params_free(struct scst_init_params *params)
{
	scst_drv_list_free(&params->drv_list);
}

static const struct spdk_json_object_decoder scst_req_init_decoders[] = {
	{"drivers", offsetof(struct scst_init_params, drv_list), scst_drv_list_decode},
};

static int
scst_req_init_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_init_req *init_req = to_init_req(req);
	struct scst_init_params *params;
	struct scst_driver *drv;
	struct scst *scst;
	int rc = 0, i;

	params = calloc(1, sizeof(*params));
	if (spdk_unlikely(!params)) {
		SPDK_ERRLOG("SCST: Failed to alloc init req params\n");
		return -ENOMEM;
	}

	if (spdk_json_decode_object(cdb, scst_req_init_decoders,
				    SPDK_COUNTOF(scst_req_init_decoders), params)) {
		SPDK_ERRLOG("Failed to decode init req params\n");
		rc = -EINVAL;
		goto free_params;
	}

	if (!params->drv_list.cnt) {
		SPDK_ERRLOG("Driver list is empty\n");
		rc = -EINVAL;
		goto free_params;
	}

	scst = req->scst;

	TAILQ_INIT(&init_req->drivers);

	for (i = 0; i < params->drv_list.cnt; i++) {
		struct scst_driver_dep *master;
		const char *drv_name = params->drv_list.names[i];

		drv = scst_find_drv_by_name(scst, drv_name);
		if (spdk_unlikely(!drv)) {
			SPDK_ERRLOG("Failed to find `%s` SCST driver\n", drv_name);
			rc = -ENOENT;
			goto free_params;
		}

		if (drv->status == DRV_LOADED) {
			continue;
		}

		TAILQ_FOREACH(master, &drv->master_list, list) {
			struct scst_driver *master_drv = master->drv;

			if (master_drv->status == DRV_UNLOADED) {
				TAILQ_INSERT_TAIL(&init_req->drivers, master_drv, list);
				master_drv->status = DRV_NEED_LOAD;
			}
		}

		TAILQ_INSERT_TAIL(&init_req->drivers, drv, list);
		drv->status = DRV_NEED_LOAD;
	}

free_params:
	scst_init_params_free(params);

	return rc;
}

static int scst_init(struct scst_req *req);

static void
scst_init_done(struct sto_subprocess *subp)
{
	struct scst_req *req = subp->priv;
	struct scst_init_req *init_req = to_init_req(req);
	struct scst_driver *drv = init_req->drv;

	SPDK_NOTICELOG("GLEB: SCST init done, driver=%s\n", drv->name);

	sto_subprocess_free(subp);

	TAILQ_REMOVE(&init_req->drivers, drv, list);
	drv->status = DRV_LOADED;

	scst_init(req);
}

static int
scst_init(struct scst_req *req)
{
	struct scst_init_req *init_req = to_init_req(req);
	struct scst_driver *drv;

	drv = TAILQ_FIRST(&init_req->drivers);
	if (drv) {
		SPDK_NOTICELOG("GLEB: modprobe driver=%s\n", drv->name);
		init_req->drv = drv;
		return scst_modprobe(drv, scst_init_done, req);
	}

	req->req_done(req->priv);

	return 0;
}

SCST_REQ_REGISTER(init)


struct scst_deinit_params {
	struct scst_drv_name_list drv_list;
};

static void
scst_deinit_params_free(struct scst_deinit_params *params)
{
	scst_drv_list_free(&params->drv_list);
}

static const struct spdk_json_object_decoder scst_req_deinit_decoders[] = {
	{"drivers", offsetof(struct scst_deinit_params, drv_list), scst_drv_list_decode},
};

static int
scst_req_deinit_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_deinit_req *deinit_req = to_deinit_req(req);
	struct scst_deinit_params *params;
	struct scst_driver *drv;
	struct scst *scst;
	int rc = 0, i;

	params = calloc(1, sizeof(*params));
	if (spdk_unlikely(!params)) {
		SPDK_ERRLOG("SCST: Failed to alloc deinit req params\n");
		return -ENOMEM;
	}

	if (spdk_json_decode_object(cdb, scst_req_deinit_decoders,
				    SPDK_COUNTOF(scst_req_deinit_decoders), params)) {
		SPDK_ERRLOG("Failed to decode deinit req params\n");
		rc = -EINVAL;
		goto free_params;
	}

	if (!params->drv_list.cnt) {
		SPDK_ERRLOG("Driver list is empty\n");
		rc = -EINVAL;
		goto free_params;
	}

	scst = req->scst;

	TAILQ_INIT(&deinit_req->drivers);

	for (i = 0; i < params->drv_list.cnt; i++) {
		struct scst_driver_dep *slave;
		const char *drv_name = params->drv_list.names[i];

		drv = scst_find_drv_by_name(scst, drv_name);
		if (spdk_unlikely(!drv)) {
			SPDK_ERRLOG("Failed to find %s SCST driver\n", drv_name);
			rc = -ENOENT;
			goto free_params;
		}

		if (drv->status == DRV_UNLOADED) {
			continue;
		}

		TAILQ_FOREACH(slave, &drv->slave_list, list) {
			struct scst_driver *slave_drv = slave->drv;
			struct scst_driver_dep *slave_dep;

			TAILQ_FOREACH(slave_dep, &slave_drv->slave_list, list) {
				struct scst_driver *slave_slave_drv = slave_dep->drv;

				if (slave_slave_drv->status == DRV_LOADED) {
					TAILQ_INSERT_TAIL(&deinit_req->drivers, slave_slave_drv, list);
					slave_slave_drv->status = DRV_NEED_UNLOAD;
				}
			}

			if (slave_drv->status == DRV_LOADED) {
				TAILQ_INSERT_TAIL(&deinit_req->drivers, slave_drv, list);
				slave_drv->status = DRV_NEED_UNLOAD;
			}
		}

		TAILQ_INSERT_TAIL(&deinit_req->drivers, drv, list);
		drv->status = DRV_NEED_UNLOAD;
	}

free_params:
	scst_deinit_params_free(params);

	return rc;
}

static int scst_deinit(struct scst_req *req);

static void
scst_deinit_done(struct sto_subprocess *subp)
{
	struct scst_req *req = subp->priv;
	struct scst_deinit_req *deinit_req = to_deinit_req(req);
	struct scst_driver *drv = deinit_req->drv;

	sto_subprocess_free(subp);

	TAILQ_REMOVE(&deinit_req->drivers, drv, list);
	drv->status = DRV_UNLOADED;

	scst_deinit(req);
}

static int
scst_deinit(struct scst_req *req)
{
	struct scst_deinit_req *deinit_req = to_deinit_req(req);
	struct scst_driver *drv;

	drv = TAILQ_FIRST(&deinit_req->drivers);
	if (drv) {
		SPDK_NOTICELOG("GLEB: rmmod driver=%s\n", drv->name);
		deinit_req->drv = drv;
		return scst_rmmod(drv, scst_deinit_done, req);
	}

	req->req_done(req->priv);

	return 0;
}

SCST_REQ_REGISTER(deinit)
