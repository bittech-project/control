#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "scst.h"
#include "scst_lib.h"
#include "sto_aio_front.h"

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
		const char *modprobe[] = {"modprobe", drv->name};

		SPDK_NOTICELOG("GLEB: modprobe driver=%s\n", drv->name);
		driver_init_req->drv = drv;

		return STO_SUBPROCESS_EXEC(modprobe, scst_driver_init_done, req);
	}

	scst_req_response(req);

	return 0;
}

static void
scst_driver_init_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

static void
scst_driver_init_req_free(struct scst_req *req)
{
	struct scst_driver_init_req *driver_init_req = to_driver_init_req(req);

	rte_free(driver_init_req);
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
		const char *rmmod[] = {"rmmod", drv->name};

		SPDK_NOTICELOG("GLEB: rmmod driver=%s\n", drv->name);
		driver_deinit_req->drv = drv;

		return STO_SUBPROCESS_EXEC(rmmod, scst_driver_deinit_done, req);
	}

	scst_req_response(req);

	return 0;
}

static void
scst_driver_deinit_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

static void
scst_driver_deinit_req_free(struct scst_req *req)
{
	struct scst_driver_deinit_req *driver_deinit_req = to_driver_deinit_req(req);

	rte_free(driver_deinit_req);
}

SCST_REQ_REGISTER(driver_deinit)

#define SCST_DEV_MAX_ATTR_CNT 32
struct scst_attr_name_list {
	const char *names[SCST_DEV_MAX_ATTR_CNT];
	size_t cnt;
};

static int
scst_attr_list_decode(const struct spdk_json_val *val, void *out)
{
	struct scst_attr_name_list *attr_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, attr_list->names,
				      SCST_DEV_MAX_ATTR_CNT, &attr_list->cnt, sizeof(char *));
}

static void
scst_attr_list_free(struct scst_attr_name_list *attr_list)
{
	ssize_t i;

	for (i = 0; i < attr_list->cnt; i++) {
		free((char *) attr_list->names[i]);
	}
}

struct scst_dev_open_params {
	char *name;
	char *handler;
	struct scst_attr_name_list attr_list;
};

static void
scst_dev_open_params_free(struct scst_dev_open_params *params)
{
	free(params->name);
	free(params->handler);
	scst_attr_list_free(&params->attr_list);
}

static const struct spdk_json_object_decoder scst_dev_open_req_decoders[] = {
	{"name", offsetof(struct scst_dev_open_params, name), spdk_json_decode_string},
	{"handler", offsetof(struct scst_dev_open_params, handler), spdk_json_decode_string},
	{"attributes", offsetof(struct scst_dev_open_params, attr_list), scst_attr_list_decode, true},
};

static int
scst_dev_open_req_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_dev_open_req *dev_open_req = to_dev_open_req(req);
	struct scst_dev_open_params params = {};
	char *parsed_cmd;
	int i, rc = 0;

	if (spdk_json_decode_object(cdb, scst_dev_open_req_decoders,
				    SPDK_COUNTOF(scst_dev_open_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode dev_open req params\n");
		return -EINVAL;
	}

	dev_open_req->mgmt_path = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
						     params.handler, SCST_MGMT_IO);
	if (spdk_unlikely(!dev_open_req->mgmt_path)) {
		SPDK_ERRLOG("Failed to alloc memory for mgmt path\n");
		rc = -ENOMEM;
		goto out;
	}

	dev_open_req->parsed_cmd = spdk_sprintf_alloc("add_device %s", params.name);
	if (spdk_unlikely(!dev_open_req->parsed_cmd)) {
		SPDK_ERRLOG("Failed to alloc memory for parsed_cmd\n");
		rc = -ENOMEM;
		goto free_mgmt_path;
	}

	for (i = 0; i < params.attr_list.cnt; i++) {
		parsed_cmd = spdk_sprintf_append_realloc(dev_open_req->parsed_cmd, " %s;",
							 params.attr_list.names[i]);
		if (spdk_unlikely(!parsed_cmd)) {
			SPDK_ERRLOG("Failed to realloc memory for parsed_cmd\n");
			rc = -ENOMEM;
			goto free_cmd;
		}

		dev_open_req->parsed_cmd = parsed_cmd;
	}

out:
	scst_dev_open_params_free(&params);

	return rc;

free_cmd:
	free(dev_open_req->parsed_cmd);

free_mgmt_path:
	free(dev_open_req->mgmt_path);

	goto out;
}

static void
scst_dev_open_done(struct sto_aio *aio)
{
	struct scst_req *req = aio->priv;
	int rc;

	rc = aio->returncode;

	sto_aio_free(aio);

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to OPEN dev\n");
		sto_err(req->ctx.err_ctx, rc);
	}

	scst_req_response(req);
}

static int
scst_dev_open_req_exec(struct scst_req *req)
{
	struct scst_dev_open_req *dev_open_req = to_dev_open_req(req);

	return sto_aio_write_string(dev_open_req->mgmt_path, dev_open_req->parsed_cmd,
				    scst_dev_open_done, req);
}

static void
scst_dev_open_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

static void
scst_dev_open_req_free(struct scst_req *req)
{
	struct scst_dev_open_req *dev_open_req = to_dev_open_req(req);

	free(dev_open_req->mgmt_path);
	free(dev_open_req->parsed_cmd);

	rte_free(dev_open_req);
}

SCST_REQ_REGISTER(dev_open)


struct scst_dev_close_params {
	char *name;
	char *handler;
};

static void
scst_dev_close_params_free(struct scst_dev_close_params *params)
{
	free(params->name);
	free(params->handler);
}

static const struct spdk_json_object_decoder scst_dev_close_req_decoders[] = {
	{"name", offsetof(struct scst_dev_close_params, name), spdk_json_decode_string},
	{"handler", offsetof(struct scst_dev_close_params, handler), spdk_json_decode_string},
};

static int
scst_dev_close_req_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_dev_close_req *dev_close_req = to_dev_close_req(req);
	struct scst_dev_close_params params = {};
	int rc = 0;

	if (spdk_json_decode_object(cdb, scst_dev_close_req_decoders,
				    SPDK_COUNTOF(scst_dev_close_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode dev_close req params\n");
		return -EINVAL;
	}

	dev_close_req->mgmt_path = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
						     params.handler, SCST_MGMT_IO);
	if (spdk_unlikely(!dev_close_req->mgmt_path)) {
		SPDK_ERRLOG("Failed to alloc memory for mgmt path\n");
		rc = -ENOMEM;
		goto out;
	}

	dev_close_req->parsed_cmd = spdk_sprintf_alloc("del_device %s", params.name);
	if (spdk_unlikely(!dev_close_req->parsed_cmd)) {
		SPDK_ERRLOG("Failed to alloc memory for parsed_cmd\n");
		rc = -ENOMEM;
		goto free_mgmt_path;
	}

out:
	scst_dev_close_params_free(&params);

	return rc;

free_mgmt_path:
	free(dev_close_req->mgmt_path);

	goto out;
}

static void
scst_dev_close_done(struct sto_aio *aio)
{
	struct scst_req *req = aio->priv;
	int rc;

	rc = aio->returncode;

	sto_aio_free(aio);

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to CLOSE dev\n");
		sto_err(req->ctx.err_ctx, rc);
	}

	scst_req_response(req);
}

static int
scst_dev_close_req_exec(struct scst_req *req)
{
	struct scst_dev_close_req *dev_close_req = to_dev_close_req(req);

	return sto_aio_write_string(dev_close_req->mgmt_path, dev_close_req->parsed_cmd,
				    scst_dev_close_done, req);
}

static void
scst_dev_close_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

static void
scst_dev_close_req_free(struct scst_req *req)
{
	struct scst_dev_close_req *dev_close_req = to_dev_close_req(req);

	free(dev_close_req->mgmt_path);
	free(dev_close_req->parsed_cmd);

	rte_free(dev_close_req);
}

SCST_REQ_REGISTER(dev_close)


struct scst_dev_resync_params {
	char *name;
};

static void
scst_dev_resync_params_free(struct scst_dev_resync_params *params)
{
	free(params->name);
}

static const struct spdk_json_object_decoder scst_dev_resync_req_decoders[] = {
	{"name", offsetof(struct scst_dev_resync_params, name), spdk_json_decode_string},
};

static int
scst_dev_resync_req_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_dev_resync_req *dev_resync_req = to_dev_resync_req(req);
	struct scst_dev_resync_params params = {};
	int rc = 0;

	if (spdk_json_decode_object(cdb, scst_dev_resync_req_decoders,
				    SPDK_COUNTOF(scst_dev_resync_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode dev_resync req params\n");
		return -EINVAL;
	}

	dev_resync_req->mgmt_path = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEVICES,
						        params.name, "resync_size");
	if (spdk_unlikely(!dev_resync_req->mgmt_path)) {
		SPDK_ERRLOG("Failed to alloc memory for mgmt path\n");
		rc = -ENOMEM;
		goto out;
	}

	dev_resync_req->parsed_cmd = "1";

out:
	scst_dev_resync_params_free(&params);

	return rc;
}

static void
scst_dev_resync_done(struct sto_aio *aio)
{
	struct scst_req *req = aio->priv;
	int rc;

	rc = aio->returncode;

	sto_aio_free(aio);

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to CLOSE dev\n");
		sto_err(req->ctx.err_ctx, rc);
	}

	scst_req_response(req);
}

static int
scst_dev_resync_req_exec(struct scst_req *req)
{
	struct scst_dev_resync_req *dev_resync_req = to_dev_resync_req(req);

	return sto_aio_write_string(dev_resync_req->mgmt_path, dev_resync_req->parsed_cmd,
				    scst_dev_resync_done, req);
}

static void
scst_dev_resync_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

static void
scst_dev_resync_req_free(struct scst_req *req)
{
	struct scst_dev_resync_req *dev_resync_req = to_dev_resync_req(req);

	free(dev_resync_req->mgmt_path);

	rte_free(dev_resync_req);
}

SCST_REQ_REGISTER(dev_resync)


static int
scst_handler_list_req_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_handler_list_req *handler_list_req = to_handler_list_req(req);

	handler_list_req->mgmt_path = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_HANDLERS);
	if (spdk_unlikely(!handler_list_req->mgmt_path)) {
		SPDK_ERRLOG("Failed to alloc memory for mgmt path\n");
		return -ENOMEM;
	}

	return 0;
}

static void
scst_handler_list_done(struct sto_readdir_ctx *ctx)
{
	struct scst_req *req = ctx->priv;
	struct scst_handler_list_req *handler_list_req = to_handler_list_req(req);
	int rc;

	rc = ctx->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to handler_list\n");
		sto_err(req->ctx.err_ctx, rc);
		goto out;
	}

	sto_dirents_init(&handler_list_req->dirents, ctx->dirents.dirents, ctx->dirents.cnt);

out:
	sto_readdir_free(ctx);

	scst_req_response(req);
}

static int
scst_handler_list_req_exec(struct scst_req *req)
{
	struct scst_handler_list_req *handler_list_req = to_handler_list_req(req);

	return sto_readdir(handler_list_req->mgmt_path, scst_handler_list_done, req);
}

static void
scst_handler_list_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	struct scst_handler_list_req *handler_list_req = to_handler_list_req(req);
	struct sto_dirents *dirents;
	int i;

	dirents = &handler_list_req->dirents;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "status", "OK");

	spdk_json_write_named_array_begin(w, "handlers");

	for (i = 0; i < dirents->cnt; i++) {
		spdk_json_write_string(w, dirents->dirents[i]);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}

static void
scst_handler_list_req_free(struct scst_req *req)
{
	struct scst_handler_list_req *handler_list_req = to_handler_list_req(req);

	free(handler_list_req->mgmt_path);
	sto_dirents_free(&handler_list_req->dirents);
}

SCST_REQ_REGISTER(handler_list)


static int
scst_device_list_req_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_device_list_req *device_list_req = to_device_list_req(req);

	device_list_req->mgmt_path = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEVICES);
	if (spdk_unlikely(!device_list_req->mgmt_path)) {
		SPDK_ERRLOG("Failed to alloc memory for mgmt path\n");
		return -ENOMEM;
	}

	return 0;
}

static void
scst_device_list_done(struct sto_readdir_ctx *ctx)
{
	struct scst_req *req = ctx->priv;
	struct scst_device_list_req *device_list_req = to_device_list_req(req);
	int rc;

	rc = ctx->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to device_list\n");
		sto_err(req->ctx.err_ctx, rc);
		goto out;
	}

	sto_dirents_init(&device_list_req->dirents, ctx->dirents.dirents, ctx->dirents.cnt);

out:
	sto_readdir_free(ctx);

	scst_req_response(req);
}

static int
scst_device_list_req_exec(struct scst_req *req)
{
	struct scst_device_list_req *device_list_req = to_device_list_req(req);

	return sto_readdir(device_list_req->mgmt_path, scst_device_list_done, req);
}

static void
scst_device_list_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	struct scst_device_list_req *device_list_req = to_device_list_req(req);
	struct sto_dirents *dirents;
	int i;

	dirents = &device_list_req->dirents;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "status", "OK");

	spdk_json_write_named_array_begin(w, "devices");

	for (i = 0; i < dirents->cnt; i++) {
		spdk_json_write_string(w, dirents->dirents[i]);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}

static void
scst_device_list_req_free(struct scst_req *req)
{
	struct scst_device_list_req *device_list_req = to_device_list_req(req);

	free(device_list_req->mgmt_path);
	sto_dirents_free(&device_list_req->dirents);
}

SCST_REQ_REGISTER(device_list)


static int
scst_target_list_req_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);

	target_list_req->mgmt_path = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
	if (spdk_unlikely(!target_list_req->mgmt_path)) {
		SPDK_ERRLOG("Failed to alloc memory for mgmt path\n");
		return -ENOMEM;
	}

	return 0;
}

static void
scst_target_list_done(struct sto_readdir_ctx *ctx)
{
	struct scst_req *req = ctx->priv;
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	int rc;

	rc = ctx->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to target_list\n");
		sto_err(req->ctx.err_ctx, rc);
		goto out;
	}

	sto_dirents_init(&target_list_req->dirents, ctx->dirents.dirents, ctx->dirents.cnt);

out:
	sto_readdir_free(ctx);

	scst_req_response(req);
}

static int
scst_target_list_req_exec(struct scst_req *req)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);

	return sto_readdir(target_list_req->mgmt_path, scst_target_list_done, req);
}

static void
scst_target_list_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	struct sto_dirents *dirents;
	int i;

	dirents = &target_list_req->dirents;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "status", "OK");

	spdk_json_write_named_array_begin(w, "targets");

	for (i = 0; i < dirents->cnt; i++) {
		spdk_json_write_string(w, dirents->dirents[i]);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}

static void
scst_target_list_req_free(struct scst_req *req)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);

	free(target_list_req->mgmt_path);
	sto_dirents_free(&target_list_req->dirents);
}

SCST_REQ_REGISTER(target_list)
