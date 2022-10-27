#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "scst.h"
#include "scst_lib.h"
#include "sto_aio_front.h"

SCST_REQ_REGISTER(write)

struct scst_write_params {
	struct sto_decoder decoder;

	struct {
		const char *(*file_path)(void *params);
		char *(*data)(void *params);
	} constructor;

	struct scst_write_req *req;
};

static int
scst_write_params_parse(void *priv, void *params)
{
	struct scst_write_params *p = priv;
	struct scst_write_req *req = p->req;
	int rc;

	req->file = p->constructor.file_path(params);
	if (spdk_unlikely(!req->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		return -ENOMEM;
	}

	req->data = p->constructor.data(params);
	if (spdk_unlikely(!req->data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		rc = -ENOMEM;
		goto free_file;
	}

	return 0;

free_file:
	free((char *) req->file);

	return rc;
}

static int
scst_write_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_write_req *write_req = to_write_req(req);
	struct scst_write_params *p = req->op->params_constructor;
	int rc = 0;

	p->req = write_req;

	rc = sto_decoder_parse(&p->decoder, cdb, scst_write_params_parse, p);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to parse CDB\n");
	}

	return rc;
}

static void
scst_write_done(struct sto_aio *aio)
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
scst_write_req_exec(struct scst_req *req)
{
	struct scst_write_req *write_req = to_write_req(req);

	return sto_aio_write_string(write_req->file, write_req->data,
				    scst_write_done, req);
}

static void
scst_write_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

static void
scst_write_req_free(struct scst_req *req)
{
	struct scst_write_req *write_req = to_write_req(req);

	free((char *) write_req->file);
	free(write_req->data);

	rte_free(write_req);
}

static struct scst_req_ops scst_write_ops = {
	.decode_cdb = scst_write_decode_cdb,
	.exec = scst_write_req_exec,
	.end_response = scst_write_req_end_response,
	.free = scst_write_req_free,
};

SCST_REQ_REGISTER(readdir)

struct scst_readdir_params {
	struct sto_decoder decoder;

	struct {
		const char *(*name)(void);
		char *(*dirpath)(void);
		int (*exclude)(const char **arr);
	} constructor;
};

static int
scst_readdir_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);
	struct scst_readdir_params *p = req->op->params_constructor;

	readdir_req->name = p->constructor.name();
	if (spdk_unlikely(!readdir_req->name)) {
		SPDK_ERRLOG("Failed to alloc memory for targets\n");
		return -ENOMEM;
	}

	readdir_req->dirpath = p->constructor.dirpath();
	if (spdk_unlikely(!readdir_req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc memory for dirpath\n");
		goto free_name;
	}

	if (p->constructor.exclude) {
		int rc;

		rc = p->constructor.exclude(readdir_req->exclude_list);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to init exclude list, rc=%d\n", rc);
			goto free_dirpath;
		}
	}

	return 0;

free_dirpath:
	free(readdir_req->dirpath);

free_name:
	free((char *) readdir_req->name);

	return -ENOMEM;
}

static void
scst_readdir_done(void *priv)
{
	struct scst_req *req = priv;
	struct scst_readdir_req *readdir_req = to_readdir_req(req);
	struct sto_readdir_result *result = &readdir_req->result;
	int rc;

	rc = result->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to readdir\n");
		sto_err(req->ctx.err_ctx, rc);
		goto out;
	}

out:
	scst_req_response(req);
}

static int
scst_readdir_req_exec(struct scst_req *req)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);
	struct sto_readdir_args args = {
		.priv = req,
		.readdir_done = scst_readdir_done,
		.result = &readdir_req->result,
	};

	return sto_readdir(readdir_req->dirpath, &args);
}

static void
scst_readdir_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);
	struct sto_readdir_result *result = &readdir_req->result;
	struct sto_dirents_json_cfg cfg = {
		.name = readdir_req->name,
		.exclude_list = readdir_req->exclude_list,
	};

	sto_dirents_info_json(&result->dirents, &cfg, w);
}

static void
__scst_readdir_req_free(struct scst_readdir_req *readdir_req)
{
	free((char *) readdir_req->name);
	free(readdir_req->dirpath);

	sto_readdir_result_free(&readdir_req->result);

	rte_free(readdir_req);
}

static void
scst_readdir_req_free(struct scst_req *req)
{
	struct scst_readdir_req *readdir_req = to_readdir_req(req);

	__scst_readdir_req_free(readdir_req);
}

static struct scst_req_ops scst_readdir_ops = {
	.decode_cdb = scst_readdir_decode_cdb,
	.exec = scst_readdir_req_exec,
	.end_response = scst_readdir_req_end_response,
	.free = scst_readdir_req_free,
};

SCST_REQ_REGISTER(target_list)

static int
scst_target_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	struct scst_readdir_req *driver_req;

	driver_req = rte_zmalloc(NULL, sizeof(struct scst_readdir_req), 0);
	if (spdk_unlikely(!driver_req)) {
		SPDK_ERRLOG("Failed to alloc driver_req\n");
		return -ENOMEM;
	}

	driver_req->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
	if (spdk_unlikely(!driver_req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc dirpath for driver_req\n");
		goto free_driver_req;
	}

	target_list_req->driver_req = driver_req;

	return 0;

free_driver_req:
	rte_free(driver_req);

	return -ENOMEM;
}

static void
scst_target_list_release(struct scst_req *req)
{
	scst_req_response(req);
}

static void
scst_target_list_get_ref(struct scst_req *req)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);

	target_list_req->refcnt++;
}

static void
scst_target_list_put_ref(struct scst_req *req)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);

	target_list_req->refcnt--;
	if (target_list_req->refcnt == 0) {
		scst_target_list_release(req);
	}
}

static void
scst_target_list_done(void *priv)
{
	struct scst_readdir_req *target_req = priv;
	struct scst_req *req = target_req->priv;
	struct sto_readdir_result *result = &target_req->result;
	int rc;

	rc = result->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to readdir targets\n");
		sto_err(req->ctx.err_ctx, rc);
	}

	scst_target_list_put_ref(req);
}

static void
scst_target_list_submit_reqs(struct scst_req *req)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	struct scst_readdir_req **target_reqs = target_list_req->target_reqs;
	int target_cnt = target_list_req->target_cnt;
	int i, rc;

	target_list_req->refcnt = 1;

	for (i = 0; i < target_cnt; i++) {
		struct scst_readdir_req *target_req = target_reqs[i];
		struct sto_readdir_args args = {
			.priv = target_req,
			.readdir_done = scst_target_list_done,
			.result = &target_req->result,
		};

		rc = sto_readdir(target_req->dirpath, &args);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to submit %d target req\n", i);
			sto_err(req->ctx.err_ctx, rc);
			break;
		}

		scst_target_list_get_ref(req);
	}

	scst_target_list_put_ref(req);

	return;
}

static struct scst_readdir_req *
scst_target_list_req_alloc(const char *path, struct sto_dirent *dirent)
{
	struct scst_readdir_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc req\n");
		return NULL;
	}

	req->name = strdup(dirent->d_name);
	if (spdk_unlikely(!req->name)) {
		SPDK_ERRLOG("Failed to alloc req name: %s\n", dirent->d_name);
		goto free_req;
	}

	req->dirpath = spdk_sprintf_alloc("%s/%s", path, dirent->d_name);
	if (spdk_unlikely(!req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc req dirpath\n");
		goto free_name;
	}

	return req;

free_name:
	free((char *) req->name);

free_req:
	rte_free(req);

	return NULL;
}

static int
scst_target_list_alloc_reqs(struct scst_req *req, struct sto_dirents *dirents)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	struct scst_readdir_req **reqs;
	int i, j, target_cnt;

	target_cnt = (int) dirents->cnt;

	reqs = rte_calloc(NULL, target_cnt, sizeof(*reqs), 0);
	if (spdk_unlikely(!reqs)) {
		SPDK_ERRLOG("Failed to alloc %d reqs\n", target_cnt);
		return -ENOMEM;
	}

	for (i = 0; i < target_cnt; i++) {
		reqs[i] = scst_target_list_req_alloc(target_list_req->driver_req->dirpath,
						     &dirents->dirents[i]);
		if (spdk_unlikely(!reqs[i])) {
			SPDK_ERRLOG("Failed to alloc %d req\n", i);
			goto free_reqs;
		}

		reqs[i]->priv = req;
	}

	target_list_req->target_reqs = reqs;
	target_list_req->target_cnt = target_cnt;

	return 0;

free_reqs:
	for (j = 0; j < i; j++) {
		rte_free(reqs[j]);
	}
	rte_free(reqs);

	return -ENOMEM;
}

static void
scst_driver_list_done(void *priv)
{
	struct scst_req *req = priv;
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	struct scst_readdir_req *driver_req = target_list_req->driver_req;
	struct sto_readdir_result *result = &driver_req->result;
	int rc;

	rc = result->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to readdir drivers\n");
		goto out_err;
	}

	rc = scst_target_list_alloc_reqs(req, &result->dirents);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to alloc target_list reqs\n");
		goto out_err;
	}

	scst_target_list_submit_reqs(req);

	return;

out_err:
	sto_err(req->ctx.err_ctx, rc);
	scst_req_response(req);
}

static int
scst_target_list_req_exec(struct scst_req *req)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	struct scst_readdir_req *driver_req = target_list_req->driver_req;
	struct sto_readdir_args args = {
		.priv = req,
		.readdir_done = scst_driver_list_done,
		.result = &driver_req->result,
	};

	return sto_readdir(driver_req->dirpath, &args);
}

static void
scst_target_list_req_end_response(struct scst_req *req, struct spdk_json_write_ctx *w)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	int i;

	spdk_json_write_array_begin(w);

	for (i = 0; i < target_list_req->target_cnt; i++) {
		struct scst_readdir_req *target_req = target_list_req->target_reqs[i];
		struct sto_readdir_result *result = &target_req->result;
		struct sto_dirents_json_cfg cfg = {
			.name = target_req->name,
			.type = S_IFDIR,
		};

		sto_dirents_info_json(&result->dirents, &cfg, w);
	}

	spdk_json_write_array_end(w);
}

static void
scst_target_list_req_free(struct scst_req *req)
{
	struct scst_target_list_req *target_list_req = to_target_list_req(req);
	int i;

	__scst_readdir_req_free(target_list_req->driver_req);

	for (i = 0; i < target_list_req->target_cnt; i++) {
		__scst_readdir_req_free(target_list_req->target_reqs[i]);
	}

	rte_free(target_list_req);
}

static struct scst_req_ops scst_target_list_ops = {
	.decode_cdb = scst_target_list_decode_cdb,
	.exec = scst_target_list_req_exec,
	.end_response = scst_target_list_req_end_response,
	.free = scst_target_list_req_free,
};

SCST_REQ_REGISTER(driver_init)

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

static struct scst_req_ops scst_driver_init_ops = {
	.decode_cdb = scst_driver_init_decode_cdb,
	.exec = scst_driver_init_req_exec,
	.end_response = scst_driver_init_req_end_response,
	.free = scst_driver_init_req_free,
};

SCST_REQ_REGISTER(driver_deinit)

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

static struct scst_req_ops scst_driver_deinit_ops = {
	.decode_cdb = scst_driver_deinit_decode_cdb,
	.exec = scst_driver_deinit_req_exec,
	.end_response = scst_driver_deinit_req_end_response,
	.free = scst_driver_deinit_req_free,
};

static const char *
scst_handler_list_name(void)
{
	return spdk_sprintf_alloc("handlers");
}

static char *
scst_handler_list_dirpath(void)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_HANDLERS);
}

static struct scst_readdir_params handler_list_constructor = {
	.constructor = {
		.name = scst_handler_list_name,
		.dirpath = scst_handler_list_dirpath,
	}
};

static const char *
scst_driver_list_name(void)
{
	return spdk_sprintf_alloc("Drivers");
}

static char *
scst_driver_list_dirpath(void)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
}

static struct scst_readdir_params driver_list_constructor = {
	.constructor = {
		.name = scst_driver_list_name,
		.dirpath = scst_driver_list_dirpath,
	}
};

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

static void *
scst_dev_open_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dev_open_params));
}

static void
scst_dev_open_params_free(void *arg)
{
	struct scst_dev_open_params *params = arg;

	free(params->name);
	free(params->handler);
	scst_attr_list_free(&params->attr_list);
	free(params);
}

static const struct spdk_json_object_decoder scst_dev_open_decoders[] = {
	{"name", offsetof(struct scst_dev_open_params, name), spdk_json_decode_string},
	{"handler", offsetof(struct scst_dev_open_params, handler), spdk_json_decode_string},
	{"attributes", offsetof(struct scst_dev_open_params, attr_list), scst_attr_list_decode, true},
};

static const char *
scst_dev_open_mgmt_file_path(void *arg)
{
	struct scst_dev_open_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
				  params->handler, SCST_MGMT_IO);
}

static char *
scst_dev_open_data(void *arg)
{
	struct scst_dev_open_params *params = arg;
	char *parsed_cmd, *data;
	int i;

	data = spdk_sprintf_alloc("add_device %s", params->name);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return NULL;
	}

	for (i = 0; i < params->attr_list.cnt; i++) {
		parsed_cmd = spdk_sprintf_append_realloc(data, " %s;",
							 params->attr_list.names[i]);
		if (spdk_unlikely(!parsed_cmd)) {
			SPDK_ERRLOG("Failed to realloc memory for data\n");
			free(data);
			return NULL;
		}

		data = parsed_cmd;
	}

	return data;
}

static struct scst_write_params dev_open_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dev_open_decoders,
					   scst_dev_open_params_alloc, scst_dev_open_params_free),
	.constructor = {
		.file_path = scst_dev_open_mgmt_file_path,
		.data = scst_dev_open_data,
	}
};

struct scst_dev_close_params {
	char *name;
	char *handler;
};

static void *
scst_dev_close_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dev_close_params));
}

static void
scst_dev_close_params_free(void *arg)
{
	struct scst_dev_close_params *params = arg;

	free(params->name);
	free(params->handler);
	free(params);
}

static const struct spdk_json_object_decoder scst_dev_close_decoders[] = {
	{"name", offsetof(struct scst_dev_close_params, name), spdk_json_decode_string},
	{"handler", offsetof(struct scst_dev_close_params, handler), spdk_json_decode_string},
};

static const char *
scst_dev_close_mgmt_file_path(void *arg)
{
	struct scst_dev_close_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
				  params->handler, SCST_MGMT_IO);
}

static char *
scst_dev_close_data(void *arg)
{
	struct scst_dev_close_params *params = arg;
	return spdk_sprintf_alloc("del_device %s", params->name);
}

static struct scst_write_params dev_close_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dev_close_decoders,
					   scst_dev_close_params_alloc, scst_dev_close_params_free),
	.constructor = {
		.file_path = scst_dev_close_mgmt_file_path,
		.data = scst_dev_close_data,
	}
};

struct scst_dev_resync_params {
	char *name;
};

static void *
scst_dev_resync_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dev_resync_params));
}

static void
scst_dev_resync_params_free(void *arg)
{
	struct scst_dev_resync_params *params = arg;
	free(params->name);
	free(params);
}

static const struct spdk_json_object_decoder scst_dev_resync_decoders[] = {
	{"name", offsetof(struct scst_dev_resync_params, name), spdk_json_decode_string},
};

static const char *
scst_dev_resync_mgmt_file_path(void *arg)
{
	struct scst_dev_resync_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEVICES,
				  params->name, "resync_size");
}

static char *
scst_dev_resync_data(void *arg)
{
	return spdk_sprintf_alloc("1");
}

static struct scst_write_params dev_resync_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dev_resync_decoders,
					   scst_dev_resync_params_alloc, scst_dev_resync_params_free),
	.constructor = {
		.file_path = scst_dev_resync_mgmt_file_path,
		.data = scst_dev_resync_data,
	}
};


static const char *
scst_dev_list_name(void)
{
	return spdk_sprintf_alloc("devices");
}

static char *
scst_dev_list_dirpath(void)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEVICES);
}

static struct scst_readdir_params dev_list_constructor = {
	.constructor = {
		.name = scst_dev_list_name,
		.dirpath = scst_dev_list_dirpath,
	}
};

struct scst_dgrp_params {
	char *name;
};

static void *
scst_dgrp_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dgrp_params));
}

static void
scst_dgrp_params_free(void *arg)
{
	struct scst_dgrp_params *params = arg;
	free(params->name);
	free(params);
}

static const struct spdk_json_object_decoder scst_dgrp_decoders[] = {
	{"name", offsetof(struct scst_dgrp_params, name), spdk_json_decode_string},
};

static const char *
scst_dgrp_mgmt_file_path(void *arg)
{
	return spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS, SCST_MGMT_IO);
}

static char *
scst_dgrp_add_data(void *arg)
{
	struct scst_dgrp_params *params = arg;
	return spdk_sprintf_alloc("create %s", params->name);
}

static char *
scst_dgrp_del_data(void *arg)
{
	struct scst_dgrp_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->name);
}

static struct scst_write_params dgrp_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_decoders,
					   scst_dgrp_params_alloc, scst_dgrp_params_free),
	.constructor = {
		.file_path = scst_dgrp_mgmt_file_path,
		.data = scst_dgrp_add_data,
	}
};

static struct scst_write_params dgrp_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_decoders,
					   scst_dgrp_params_alloc, scst_dgrp_params_free),
	.constructor = {
		.file_path = scst_dgrp_mgmt_file_path,
		.data = scst_dgrp_del_data,
	}
};

static const char *
scst_dgrp_list_name(void)
{
	return spdk_sprintf_alloc("Device Group");
}

static char *
scst_dgrp_list_dirpath(void)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEV_GROUPS);
}

static int
scst_dgrp_list_exclude(const char **exclude_list)
{
	exclude_list[0] = SCST_MGMT_IO;

	return 0;
}

static struct scst_readdir_params dgrp_list_constructor = {
	.constructor = {
		.name = scst_dgrp_list_name,
		.dirpath = scst_dgrp_list_dirpath,
		.exclude = scst_dgrp_list_exclude,
	}
};

struct scst_dgrp_dev_params {
	char *dgrp_name;
	char *dev_name;
};

static void *
scst_dgrp_dev_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dgrp_dev_params));
}

static void
scst_dgrp_dev_params_free(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;

	free(params->dgrp_name);
	free(params->dev_name);
	free(params);
}

static const struct spdk_json_object_decoder scst_dgrp_dev_decoders[] = {
	{"dgrp_name", offsetof(struct scst_dgrp_dev_params, dgrp_name), spdk_json_decode_string},
	{"dev_name", offsetof(struct scst_dgrp_dev_params, dev_name), spdk_json_decode_string},
};

static const char *
scst_dgrp_dev_mgmt_file_path(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->dgrp_name, "devices", SCST_MGMT_IO);
}

static char *
scst_dgrp_add_dev_data(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;
	return spdk_sprintf_alloc("add %s", params->dev_name);
}

static char *
scst_dgrp_del_dev_data(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->dev_name);
}

static struct scst_write_params dgrp_add_dev_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_dev_decoders,
					   scst_dgrp_dev_params_alloc, scst_dgrp_dev_params_free),
	.constructor = {
		.file_path = scst_dgrp_dev_mgmt_file_path,
		.data = scst_dgrp_add_dev_data,
	}
};

static struct scst_write_params dgrp_del_dev_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_dev_decoders,
					   scst_dgrp_dev_params_alloc, scst_dgrp_dev_params_free),
	.constructor = {
		.file_path = scst_dgrp_dev_mgmt_file_path,
		.data = scst_dgrp_del_dev_data,
	}
};

struct scst_target_params {
	char *target;
	char *driver;
};

static void *
scst_target_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_target_params));
}

static void
scst_target_params_free(void *arg)
{
	struct scst_target_params *params = arg;

	free(params->target);
	free(params->driver);
	free(params);
}

static const struct spdk_json_object_decoder scst_target_decoders[] = {
	{"target", offsetof(struct scst_target_params, target), spdk_json_decode_string},
	{"driver", offsetof(struct scst_target_params, driver), spdk_json_decode_string},
};

static const char *
scst_target_mgmt_file_path(void *arg)
{
	struct scst_target_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  params->driver, SCST_MGMT_IO);
}

static char *
scst_target_add_data(void *arg)
{
	struct scst_target_params *params = arg;
	return spdk_sprintf_alloc("add_target %s", params->target);
}

static char *
scst_target_del_data(void *arg)
{
	struct scst_target_params *params = arg;
	return spdk_sprintf_alloc("del_target %s", params->target);
}

static struct scst_write_params target_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.constructor = {
		.file_path = scst_target_mgmt_file_path,
		.data = scst_target_add_data,
	}
};

static struct scst_write_params target_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.constructor = {
		.file_path = scst_target_mgmt_file_path,
		.data = scst_target_del_data,
	}
};

struct scst_group_params {
	char *group;
	char *driver;
	char *target;
};

static void *
scst_group_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_group_params));
}

static void
scst_group_params_free(void *arg)
{
	struct scst_group_params *params = arg;

	free(params->group);
	free(params->driver);
	free(params->target);
	free(params);
}

static const struct spdk_json_object_decoder scst_group_decoders[] = {
	{"group", offsetof(struct scst_group_params, group), spdk_json_decode_string},
	{"driver", offsetof(struct scst_group_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_group_params, target), spdk_json_decode_string},
};

static const char *
scst_group_mgmt_file_path(void *arg)
{
	struct scst_group_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  params->driver, params->target, "ini_groups", SCST_MGMT_IO);
}

static char *
scst_group_add_data(void *arg)
{
	struct scst_group_params *params = arg;
	return spdk_sprintf_alloc("create %s", params->group);
}

static char *
scst_group_del_data(void *arg)
{
	struct scst_group_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->group);
}

static struct scst_write_params group_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_group_decoders,
					   scst_group_params_alloc, scst_group_params_free),
	.constructor = {
		.file_path = scst_group_mgmt_file_path,
		.data = scst_group_add_data,
	}
};

static struct scst_write_params group_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_group_decoders,
					   scst_group_params_alloc, scst_group_params_free),
	.constructor = {
		.file_path = scst_group_mgmt_file_path,
		.data = scst_group_del_data,
	}
};


static const struct scst_cdbops scst_op_table[] = {
	{
		.op.name = "driver_init",
		.req_constructor = scst_driver_init_req_constructor,
		.req_ops = &scst_driver_init_ops,
	},
	{
		.op.name = "driver_deinit",
		.req_constructor = scst_driver_deinit_req_constructor,
		.req_ops = &scst_driver_deinit_ops,
	},
	{
		.op.name = "handler_list",
		.req_constructor = scst_readdir_req_constructor,
		.req_ops = &scst_readdir_ops,
		.params_constructor = &handler_list_constructor,
	},
	{
		.op.name = "driver_list",
		.req_constructor = scst_readdir_req_constructor,
		.req_ops = &scst_readdir_ops,
		.params_constructor = &driver_list_constructor,
	},
	{
		.op.name = "dev_open",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &dev_open_constructor,
	},
	{
		.op.name = "dev_close",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &dev_close_constructor,
	},
	{
		.op.name = "dev_resync",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &dev_resync_constructor,
	},
	{
		.op.name = "dev_list",
		.req_constructor = scst_readdir_req_constructor,
		.req_ops = &scst_readdir_ops,
		.params_constructor = &dev_list_constructor,
	},
	{
		.op.name = "dgrp_add",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &dgrp_add_constructor,
	},
	{
		.op.name = "dgrp_del",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &dgrp_del_constructor,
	},
	{
		.op.name = "dgrp_list",
		.req_constructor = scst_readdir_req_constructor,
		.req_ops = &scst_readdir_ops,
		.params_constructor = &dgrp_list_constructor,
	},
	{
		.op.name = "dgrp_add_dev",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &dgrp_add_dev_constructor,
	},
	{
		.op.name = "dgrp_del_dev",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &dgrp_del_dev_constructor,
	},
	{
		.op.name = "target_add",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &target_add_constructor,
	},
	{
		.op.name = "target_del",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &target_del_constructor,
	},
	{
		.op.name = "target_list",
		.req_constructor = scst_target_list_req_constructor,
		.req_ops = &scst_target_list_ops,
	},
	{
		.op.name = "group_add",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &group_add_constructor,
	},
	{
		.op.name = "group_del",
		.req_constructor = scst_write_req_constructor,
		.req_ops = &scst_write_ops,
		.params_constructor = &group_del_constructor,
	},
};

#define SCST_OP_TBL_SIZE	(SPDK_COUNTOF(scst_op_table))

const struct scst_cdbops *
scst_find_cdbops(const char *op_name)
{
	int i;

	for (i = 0; i < SCST_OP_TBL_SIZE; i++) {
		const struct scst_cdbops *op = &scst_op_table[i];

		if (!strcmp(op_name, op->op.name)) {
			return op;
		}
	}

	return NULL;
}
