#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "scst.h"
#include "scst_lib.h"
#include "sto_aio_front.h"

static void
scst_write_file_done(struct sto_aio *aio)
{
	struct scst_req *req = aio->priv;
	int rc;

	rc = aio->returncode;

	sto_aio_free(aio);

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to device group add\n");
		sto_err(req->ctx.err_ctx, rc);
	}

	scst_req_response(req);
}

static int
scst_write_file_req_exec(struct scst_req *req)
{
	struct scst_write_file_req *write_file_req = to_write_file_req(req);

	return sto_aio_write_string(write_file_req->file, write_file_req->data,
				    scst_write_file_done, req);
}

static void
scst_write_file_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

static void
scst_write_file_req_free(struct scst_req *req)
{
	struct scst_write_file_req *write_file_req = to_write_file_req(req);

	free((char *) write_file_req->file);
	free(write_file_req->data);

	rte_free(write_file_req);
}
SCST_REQ_REGISTER(write_file)


static void
scst_readdir_done(struct sto_readdir_req *rd_req)
{
	struct scst_req *req = rd_req->priv;
	struct scst_readdir_req *scst_rd_req = to_readdir_req(req);
	int rc;

	rc = rd_req->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to readdir\n");
		sto_err(req->ctx.err_ctx, rc);
		goto out;
	}

	sto_dirents_init(&scst_rd_req->dirents, rd_req->dirents.entries, rd_req->dirents.cnt);

out:
	sto_readdir_free(rd_req);

	scst_req_response(req);
}

static int
scst_readdir_req_exec(struct scst_req *req)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);

	return sto_readdir(readdir_req->dirpath, scst_readdir_done, req);
}

static void
scst_readdir_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);
	struct sto_dirents *dirents;

	dirents = &readdir_req->dirents;

	spdk_json_write_array_begin(w);

	sto_status_ok(w);
	sto_dirents_dump_json(readdir_req->name, readdir_req->exclude_str,
			      dirents, w);

	spdk_json_write_array_end(w);
}

static void
scst_readdir_req_free(struct scst_req *req)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);

	free((char *) readdir_req->name);
	free(readdir_req->dirpath);

	free((char *) readdir_req->exclude_str);

	sto_dirents_free(&readdir_req->dirents);

	rte_free(readdir_req);
}

SCST_REQ_REGISTER(readdir)


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

int
scst_driver_init_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
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

int
scst_driver_deinit_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
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

/* OP_HANDLER_LIST */

int
scst_handler_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);

	readdir_req->name = spdk_sprintf_alloc("handlers");
	if (spdk_unlikely(!readdir_req->name)) {
		SPDK_ERRLOG("Failed to alloc memory for handlers\n");
		return -ENOMEM;
	}

	readdir_req->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_HANDLERS);
	if (spdk_unlikely(!readdir_req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc memory for dirpath\n");
		goto free_name;
	}

	return 0;

free_name:
	free((char *) readdir_req->name);

	return -ENOMEM;
}

/* OP_DEV_OPEN */

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

int
scst_dev_open_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_write_file_req *write_file_req = to_write_file_req(req);
	struct scst_dev_open_params params = {};
	char *parsed_cmd;
	int i, rc = 0;

	if (spdk_json_decode_object(cdb, scst_dev_open_req_decoders,
				    SPDK_COUNTOF(scst_dev_open_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode dev_open req params\n");
		return -EINVAL;
	}

	write_file_req->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
						  params.handler, SCST_MGMT_IO);
	if (spdk_unlikely(!write_file_req->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		rc = -ENOMEM;
		goto out;
	}

	write_file_req->data = spdk_sprintf_alloc("add_device %s", params.name);
	if (spdk_unlikely(!write_file_req->data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		rc = -ENOMEM;
		goto free_file;
	}

	for (i = 0; i < params.attr_list.cnt; i++) {
		parsed_cmd = spdk_sprintf_append_realloc(write_file_req->data, " %s;",
							 params.attr_list.names[i]);
		if (spdk_unlikely(!parsed_cmd)) {
			SPDK_ERRLOG("Failed to realloc memory for data\n");
			rc = -ENOMEM;
			goto free_data;
		}

		write_file_req->data = parsed_cmd;
	}

out:
	scst_dev_open_params_free(&params);

	return rc;

free_data:
	free(write_file_req->data);

free_file:
	free((char *) write_file_req->file);

	goto out;
}

/* OP_DEV_CLOSE */

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

int
scst_dev_close_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_write_file_req *write_file_req = to_write_file_req(req);
	struct scst_dev_close_params params = {};
	int rc = 0;

	if (spdk_json_decode_object(cdb, scst_dev_close_req_decoders,
				    SPDK_COUNTOF(scst_dev_close_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode dev_close req params\n");
		return -EINVAL;
	}

	write_file_req->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
						  params.handler, SCST_MGMT_IO);
	if (spdk_unlikely(!write_file_req->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		rc = -ENOMEM;
		goto out;
	}

	write_file_req->data = spdk_sprintf_alloc("del_device %s", params.name);
	if (spdk_unlikely(!write_file_req->data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		rc = -ENOMEM;
		goto free_file;
	}

out:
	scst_dev_close_params_free(&params);

	return rc;

free_file:
	free((char *) write_file_req->file);

	goto out;
}

/* OP_DEV_RESYNC */

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

int
scst_dev_resync_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_write_file_req *write_file_req = to_write_file_req(req);
	struct scst_dev_resync_params params = {};
	int rc = 0;

	if (spdk_json_decode_object(cdb, scst_dev_resync_req_decoders,
				    SPDK_COUNTOF(scst_dev_resync_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode dev_resync req params\n");
		return -EINVAL;
	}

	write_file_req->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEVICES,
						  params.name, "resync_size");
	if (spdk_unlikely(!write_file_req->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		rc = -ENOMEM;
		goto out;
	}

	write_file_req->data = strdup("1");
	if (spdk_unlikely(!write_file_req->data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		rc = -ENOMEM;
		goto free_file;
	}

out:
	scst_dev_resync_params_free(&params);

	return rc;

free_file:
	free((char *) write_file_req->file);

	goto out;
}

/* OP_DEV_LIST */

int
scst_dev_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);

	readdir_req->name = spdk_sprintf_alloc("devices");
	if (spdk_unlikely(!readdir_req->name)) {
		SPDK_ERRLOG("Failed to alloc memory for devices\n");
		return -ENOMEM;
	}

	readdir_req->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEVICES);
	if (spdk_unlikely(!readdir_req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc memory for dirpath\n");
		goto free_name;
	}

	return 0;

free_name:
	free((char *) readdir_req->name);

	return -ENOMEM;
}

/* OP_DGRP_ADD */

struct scst_dgrp_add_params {
	char *name;
};

static void
scst_dgrp_add_params_free(struct scst_dgrp_add_params *params)
{
	free(params->name);
}

static const struct spdk_json_object_decoder scst_dgrp_add_req_decoders[] = {
	{"name", offsetof(struct scst_dgrp_add_params, name), spdk_json_decode_string},
};

int
scst_dgrp_add_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_write_file_req *write_file_req = to_write_file_req(req);
	struct scst_dgrp_add_params params = {};
	int rc = 0;

	if (spdk_json_decode_object(cdb, scst_dgrp_add_req_decoders,
				    SPDK_COUNTOF(scst_dgrp_add_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode dgrp_add req params\n");
		return -EINVAL;
	}

	write_file_req->file = spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
						  SCST_MGMT_IO);
	if (spdk_unlikely(!write_file_req->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		rc = -ENOMEM;
		goto out;
	}

	write_file_req->data = spdk_sprintf_alloc("create %s", params.name);
	if (spdk_unlikely(!write_file_req->data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		rc = -ENOMEM;
		goto free_file;
	}

out:
	scst_dgrp_add_params_free(&params);

	return rc;

free_file:
	free((char *) write_file_req->file);

	goto out;
}

/* OP_DGRP_DEL */

struct scst_dgrp_del_params {
	char *name;
};

static void
scst_dgrp_del_params_free(struct scst_dgrp_del_params *params)
{
	free(params->name);
}

static const struct spdk_json_object_decoder scst_dgrp_del_req_decoders[] = {
	{"name", offsetof(struct scst_dgrp_del_params, name), spdk_json_decode_string},
};

int
scst_dgrp_del_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_write_file_req *write_file_req = to_write_file_req(req);
	struct scst_dgrp_del_params params = {};
	int rc = 0;

	if (spdk_json_decode_object(cdb, scst_dgrp_del_req_decoders,
				    SPDK_COUNTOF(scst_dgrp_del_req_decoders), &params)) {
		SPDK_ERRLOG("Failed to decode dgrp_del req params\n");
		return -EINVAL;
	}

	write_file_req->file = spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
						  SCST_MGMT_IO);
	if (spdk_unlikely(!write_file_req->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		rc = -ENOMEM;
		goto out;
	}

	write_file_req->data = spdk_sprintf_alloc("del %s", params.name);
	if (spdk_unlikely(!write_file_req->data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		rc = -ENOMEM;
		goto free_file;
	}

out:
	scst_dgrp_del_params_free(&params);

	return rc;

free_file:
	free((char *) write_file_req->file);

	goto out;
}

/* OP_DGRP_LIST */

int
scst_dgrp_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);

	readdir_req->name = spdk_sprintf_alloc("Device Group");
	if (spdk_unlikely(!readdir_req->name)) {
		SPDK_ERRLOG("Failed to alloc memory for Device Group\n");
		return -ENOMEM;
	}

	readdir_req->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEV_GROUPS);
	if (spdk_unlikely(!readdir_req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc memory for dirpath\n");
		goto free_name;
	}

	readdir_req->exclude_str = spdk_sprintf_alloc(SCST_MGMT_IO);
	if (spdk_unlikely(!readdir_req->exclude_str)) {
		SPDK_ERRLOG("Failed to alloc memory for exclude_str\n");
		goto free_dirpath;
	}

	return 0;

free_dirpath:
	free(readdir_req->dirpath);

free_name:
	free((char *) readdir_req->name);

	return -ENOMEM;
}

/* OP_TARGET_LIST */

int
scst_target_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);

	readdir_req->name = spdk_sprintf_alloc("targets");
	if (spdk_unlikely(!readdir_req->name)) {
		SPDK_ERRLOG("Failed to alloc memory for targets\n");
		return -ENOMEM;
	}

	readdir_req->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
	if (spdk_unlikely(!readdir_req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc memory for dirpath\n");
		goto free_name;
	}

	return 0;

free_name:
	free((char *) readdir_req->name);

	return -ENOMEM;
}
