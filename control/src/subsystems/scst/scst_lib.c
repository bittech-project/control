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

struct scst_driver_init_params {
	struct scst_drv_name_list drv_list;
};

static void
scst_driver_init_params_free(struct scst_driver_init_params *params)
{
	scst_drv_list_free(&params->drv_list);
}

static const struct spdk_json_object_decoder scst_driver_init_req_decoders[] = {
	{"drivers", offsetof(struct scst_driver_init_params, drv_list), scst_drv_list_decode},
};

static int
scst_driver_init_req_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_driver_init_req *driver_init_req = to_driver_init_req(req);
	struct scst_driver_init_params params = {};
	struct scst_driver *drv;
	struct scst *scst;
	int rc = 0, i;

	if (spdk_json_decode_object(cdb, scst_driver_init_req_decoders,
				    SPDK_COUNTOF(scst_driver_init_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode driver_init req params\n");
		return -EINVAL;
	}

	if (!params.drv_list.cnt) {
		SPDK_ERRLOG("Driver list is empty\n");
		rc = -EINVAL;
		goto out;
	}

	scst = req->scst;

	TAILQ_INIT(&driver_init_req->drivers);

	for (i = 0; i < params.drv_list.cnt; i++) {
		struct scst_driver_dep *master;
		const char *drv_name = params.drv_list.names[i];

		drv = scst_find_drv_by_name(scst, drv_name);
		if (spdk_unlikely(!drv)) {
			SPDK_ERRLOG("Failed to find `%s` SCST driver\n", drv_name);
			rc = -ENOENT;
			goto out;
		}

		if (drv->status == DRV_LOADED) {
			continue;
		}

		TAILQ_FOREACH(master, &drv->master_list, list) {
			struct scst_driver *master_drv = master->drv;

			if (master_drv->status == DRV_UNLOADED) {
				TAILQ_INSERT_TAIL(&driver_init_req->drivers, master_drv, list);
				master_drv->status = DRV_NEED_LOAD;
			}
		}

		TAILQ_INSERT_TAIL(&driver_init_req->drivers, drv, list);
		drv->status = DRV_NEED_LOAD;
	}

out:
	scst_driver_init_params_free(&params);

	return rc;
}

static int scst_driver_init_req_exec(struct scst_req *req);

static void
scst_driver_init_done(struct sto_subprocess *subp)
{
	struct scst_req *req = subp->priv;
	struct scst_driver_init_req *driver_init_req = to_driver_init_req(req);
	struct scst_driver *drv = driver_init_req->drv;
	int rc;

	rc = subp->returncode;

	sto_subprocess_free(subp);

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to modprobe driver %s\n", drv->name);
		sto_err(req->ctx.err_ctx, rc);
		scst_req_response(req);
		return;
	}

	TAILQ_REMOVE(&driver_init_req->drivers, drv, list);
	drv->status = DRV_LOADED;

	rc = scst_driver_init_req_exec(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to 'scst_driver_init' for driver %s\n", drv->name);
		sto_err(req->ctx.err_ctx, rc);
		scst_req_response(req);
		return;
	}

	return;
}

static int
scst_driver_init_req_exec(struct scst_req *req)
{
	struct scst_driver_init_req *driver_init_req = to_driver_init_req(req);
	struct scst_driver *drv;

	drv = TAILQ_FIRST(&driver_init_req->drivers);
	if (drv) {
		SPDK_NOTICELOG("GLEB: modprobe driver=%s\n", drv->name);
		driver_init_req->drv = drv;
		return scst_modprobe(drv, scst_driver_init_done, req);
	}

	scst_req_response(req);

	return 0;
}

static void
scst_driver_init_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "status", 0);

	spdk_json_write_object_end(w);
}

SCST_REQ_REGISTER(driver_init)


struct scst_driver_deinit_params {
	struct scst_drv_name_list drv_list;
};

static void
scst_driver_deinit_params_free(struct scst_driver_deinit_params *params)
{
	scst_drv_list_free(&params->drv_list);
}

static const struct spdk_json_object_decoder scst_driver_deinit_req_decoders[] = {
	{"drivers", offsetof(struct scst_driver_deinit_params, drv_list), scst_drv_list_decode},
};

static int
scst_driver_deinit_req_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_driver_deinit_req *driver_deinit_req = to_driver_deinit_req(req);
	struct scst_driver_deinit_params params = {};
	struct scst_driver *drv;
	struct scst *scst;
	int rc = 0, i;

	if (spdk_json_decode_object(cdb, scst_driver_deinit_req_decoders,
				    SPDK_COUNTOF(scst_driver_deinit_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode driver_deinit req params\n");
		return -EINVAL;
	}

	if (!params.drv_list.cnt) {
		SPDK_ERRLOG("Driver list is empty\n");
		rc = -EINVAL;
		goto out;
	}

	scst = req->scst;

	TAILQ_INIT(&driver_deinit_req->drivers);

	for (i = 0; i < params.drv_list.cnt; i++) {
		struct scst_driver_dep *slave;
		const char *drv_name = params.drv_list.names[i];

		drv = scst_find_drv_by_name(scst, drv_name);
		if (spdk_unlikely(!drv)) {
			SPDK_ERRLOG("Failed to find %s SCST driver\n", drv_name);
			rc = -ENOENT;
			goto out;
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
					TAILQ_INSERT_TAIL(&driver_deinit_req->drivers, slave_slave_drv, list);
					slave_slave_drv->status = DRV_NEED_UNLOAD;
				}
			}

			if (slave_drv->status == DRV_LOADED) {
				TAILQ_INSERT_TAIL(&driver_deinit_req->drivers, slave_drv, list);
				slave_drv->status = DRV_NEED_UNLOAD;
			}
		}

		TAILQ_INSERT_TAIL(&driver_deinit_req->drivers, drv, list);
		drv->status = DRV_NEED_UNLOAD;
	}

out:
	scst_driver_deinit_params_free(&params);

	return rc;
}

static int scst_driver_deinit_req_exec(struct scst_req *req);

static void
scst_driver_deinit_done(struct sto_subprocess *subp)
{
	struct scst_req *req = subp->priv;
	struct scst_driver_deinit_req *driver_deinit_req = to_driver_deinit_req(req);
	struct scst_driver *drv = driver_deinit_req->drv;
	int rc;

	rc = subp->returncode;

	sto_subprocess_free(subp);

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to rmmod driver %s\n", drv->name);
		sto_err(req->ctx.err_ctx, rc);
		scst_req_response(req);
		return;
	}

	TAILQ_REMOVE(&driver_deinit_req->drivers, drv, list);
	drv->status = DRV_UNLOADED;

	rc = scst_driver_deinit_req_exec(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to 'scst_driver_deinit' for driver %s\n", drv->name);
		sto_err(req->ctx.err_ctx, rc);
		scst_req_response(req);
		return;
	}
}

static int
scst_driver_deinit_req_exec(struct scst_req *req)
{
	struct scst_driver_deinit_req *driver_deinit_req = to_driver_deinit_req(req);
	struct scst_driver *drv;

	drv = TAILQ_FIRST(&driver_deinit_req->drivers);
	if (drv) {
		SPDK_NOTICELOG("GLEB: rmmod driver=%s\n", drv->name);
		driver_deinit_req->drv = drv;
		return scst_rmmod(drv, scst_driver_deinit_done, req);
	}

	scst_req_response(req);

	return 0;
}

static void
scst_driver_deinit_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "status", 0);

	spdk_json_write_object_end(w);
}

SCST_REQ_REGISTER(driver_deinit)
