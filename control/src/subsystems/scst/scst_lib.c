#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "scst.h"
#include "scst_lib.h"

struct sto_req *
scst_tg_list_req_constructor(const struct sto_cdbops *op)
{
	struct scst_tg_list_req *tg_list_req;

	tg_list_req = rte_zmalloc(NULL, sizeof(*tg_list_req), 0);
	if (spdk_unlikely(!tg_list_req)) {
		SPDK_ERRLOG("Failed to alloc sto ls req\n");
		return NULL;
	}

	sto_req_init(&tg_list_req->req, op);

	return &tg_list_req->req;
}

static int
scst_tg_list_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);

	tg_list_req->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
	if (spdk_unlikely(!tg_list_req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc dirpath for tg_list_req\n");
		return -ENOMEM;
	}

	return 0;
}

static void
scst_tg_list_req_release(struct sto_req *req)
{
	sto_req_response(req);
}

static void
scst_tg_list_req_get_ref(struct sto_req *req)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);

	tg_list_req->refcnt++;
}

static void
scst_tg_list_req_put_ref(struct sto_req *req)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);

	tg_list_req->refcnt--;
	if (tg_list_req->refcnt == 0) {
		scst_tg_list_req_release(req);
	}
}

static void
scst_tg_list_done(void *priv)
{
	struct scst_ls_tg_req *tg_req = priv;
	struct sto_req *req = tg_req->priv;
	struct sto_readdir_result *result = &tg_req->result;
	int rc;

	rc = result->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to readdir targets\n");
		sto_err(req->ctx.err_ctx, rc);
	}

	scst_tg_list_req_put_ref(req);
}

static void
scst_tg_list_submit_reqs(struct sto_req *req)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);
	int i, rc;

	tg_list_req->refcnt = 1;

	for (i = 0; i < tg_list_req->driver_cnt; i++) {
		struct scst_ls_tg_req *tg_req = &tg_list_req->tg_reqs[i];
		struct sto_readdir_args args = {
			.priv = tg_req,
			.readdir_done = scst_tg_list_done,
			.result = &tg_req->result,
		};

		rc = sto_readdir(tg_req->dirpath, &args);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to submit %d target req\n", i);
			sto_err(req->ctx.err_ctx, rc);
			break;
		}

		scst_tg_list_req_get_ref(req);
	}

	scst_tg_list_req_put_ref(req);

	return;
}

static int
scst_tg_list_reqs_alloc(struct sto_req *req, struct sto_dirents *dirents)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);
	int i, j;

	tg_list_req->tg_reqs = rte_calloc(NULL, tg_list_req->driver_cnt, sizeof(struct scst_ls_tg_req), 0);
	if (spdk_unlikely(!tg_list_req->tg_reqs)) {
		SPDK_ERRLOG("Failed to alloc tg reqs\n");
		return -ENOMEM;
	}

	for (i = 0; i < tg_list_req->driver_cnt; i++) {
		struct scst_ls_tg_req *tg_req = &tg_list_req->tg_reqs[i];

		tg_req->name = strdup(dirents->dirents[i].name);
		if (spdk_unlikely(!tg_req->name)) {
			SPDK_ERRLOG("Failed to alloc name for tg req\n");
			goto out_err;
		}

		tg_req->dirpath = spdk_sprintf_alloc("%s/%s", tg_list_req->dirpath, tg_req->name);
		if (spdk_unlikely(!tg_req->dirpath)) {
			SPDK_ERRLOG("Failed to alloc dirpath for tg req\n");
			free((char *) tg_req->name);
			goto out_err;
		}

		tg_req->priv = req;
	}

	return 0;

out_err:
	for (j = 0; j < i; j++) {
		struct scst_ls_tg_req *tg_req = &tg_list_req->tg_reqs[j];

		free((char *) tg_req->name);
		free(tg_req->dirpath);
	}

	rte_free(tg_list_req->tg_reqs);

	return -ENOMEM;
}

static void
scst_tg_list_reqs_free(struct sto_req *req)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);
	int i;

	for (i = 0; i < tg_list_req->driver_cnt; i++) {
		struct scst_ls_tg_req *tg_req = &tg_list_req->tg_reqs[i];

		free((char *) tg_req->name);
		free(tg_req->dirpath);

		sto_readdir_result_free(&tg_req->result);
	}

	rte_free(tg_list_req->tg_reqs);
}

static void
scst_driver_info_done(void *priv)
{
	struct sto_req *req = priv;
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);
	struct sto_readdir_result *result = &tg_list_req->driver_info;
	int rc;

	rc = result->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to readdir drivers\n");
		goto out_err;
	}

	tg_list_req->driver_cnt = result->dirents.cnt;

	rc = scst_tg_list_reqs_alloc(req, &result->dirents);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to alloc %d target reqs\n",
				tg_list_req->driver_cnt);
		goto out_err;
	}

	scst_tg_list_submit_reqs(req);

	return;

out_err:
	sto_err(req->ctx.err_ctx, rc);
	sto_req_response(req);
}

static int
scst_tg_list_req_exec(struct sto_req *req)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);
	struct sto_readdir_args args = {
		.priv = req,
		.readdir_done = scst_driver_info_done,
		.result = &tg_list_req->driver_info,
	};

	return sto_readdir(tg_list_req->dirpath, &args);
}

static void
scst_tg_list_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);
	int i;

	spdk_json_write_array_begin(w);

	for (i = 0; i < tg_list_req->driver_cnt; i++) {
		struct scst_ls_tg_req *tg_req = &tg_list_req->tg_reqs[i];
		struct sto_readdir_result *result = &tg_req->result;
		struct sto_dirents_json_cfg cfg = {
			.name = tg_req->name,
			.type = S_IFDIR,
		};

		sto_dirents_info_json(&result->dirents, &cfg, w);
	}

	spdk_json_write_array_end(w);
}

static void
scst_tg_list_req_free(struct sto_req *req)
{
	struct scst_tg_list_req *tg_list_req = to_tg_list_req(req);

	free(tg_list_req->dirpath);

	sto_readdir_result_free(&tg_list_req->driver_info);

	scst_tg_list_reqs_free(req);

	rte_free(tg_list_req);
}

static struct sto_req_ops scst_tg_list_req_ops = {
	.decode_cdb = scst_tg_list_req_decode_cdb,
	.exec = scst_tg_list_req_exec,
	.end_response = scst_tg_list_req_end_response,
	.free = scst_tg_list_req_free,
};

static const char *
scst_handler_list_name(void *arg)
{
	return spdk_sprintf_alloc("handlers");
}

static char *
scst_handler_list_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_HANDLERS);
}

static struct sto_ls_req_params handler_list_constructor = {
	.constructor = {
		.name = scst_handler_list_name,
		.dirpath = scst_handler_list_dirpath,
	}
};

static const char *
scst_driver_list_name(void *arg)
{
	return spdk_sprintf_alloc("Drivers");
}

static char *
scst_driver_list_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
}

static struct sto_ls_req_params driver_list_constructor = {
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

static int
scst_attr_list_fill_data(struct scst_attr_name_list *attr_list, char **data)
{
	char *parsed_cmd;
	int i;

	for (i = 0; i < attr_list->cnt; i++) {
		parsed_cmd = spdk_sprintf_append_realloc(*data, " %s;",
							 attr_list->names[i]);
		if (spdk_unlikely(!parsed_cmd)) {
			SPDK_ERRLOG("Failed to realloc memory for attributes data\n");
			return -ENOMEM;
		}

		*data = parsed_cmd;
	}

	return 0;
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
	char *data;
	int rc;

	data = spdk_sprintf_alloc("add_device %s", params->name);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return NULL;
	}

	rc = scst_attr_list_fill_data(&params->attr_list, &data);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to fill scst attrs\n");
		free(data);
		return NULL;
	}

	return data;
}

static struct sto_write_req_params dev_open_constructor = {
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

static struct sto_write_req_params dev_close_constructor = {
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

static struct sto_write_req_params dev_resync_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dev_resync_decoders,
					   scst_dev_resync_params_alloc, scst_dev_resync_params_free),
	.constructor = {
		.file_path = scst_dev_resync_mgmt_file_path,
		.data = scst_dev_resync_data,
	}
};


static const char *
scst_dev_list_name(void *arg)
{
	return spdk_sprintf_alloc("devices");
}

static char *
scst_dev_list_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEVICES);
}

static struct sto_ls_req_params dev_list_constructor = {
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

static struct sto_write_req_params dgrp_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_decoders,
					   scst_dgrp_params_alloc, scst_dgrp_params_free),
	.constructor = {
		.file_path = scst_dgrp_mgmt_file_path,
		.data = scst_dgrp_add_data,
	}
};

static struct sto_write_req_params dgrp_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_decoders,
					   scst_dgrp_params_alloc, scst_dgrp_params_free),
	.constructor = {
		.file_path = scst_dgrp_mgmt_file_path,
		.data = scst_dgrp_del_data,
	}
};

static const char *
scst_dgrp_list_name(void *arg)
{
	return spdk_sprintf_alloc("Device Group");
}

static char *
scst_dgrp_list_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEV_GROUPS);
}

static int
scst_dgrp_list_exclude(const char **exclude_list)
{
	exclude_list[0] = SCST_MGMT_IO;

	return 0;
}

static struct sto_ls_req_params dgrp_list_constructor = {
	.constructor = {
		.name = scst_dgrp_list_name,
		.dirpath = scst_dgrp_list_dirpath,
		.exclude = scst_dgrp_list_exclude,
	}
};

struct scst_dgrp_dev_params {
	char *name;
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

	free(params->name);
	free(params->dev_name);
	free(params);
}

static const struct spdk_json_object_decoder scst_dgrp_dev_decoders[] = {
	{"name", offsetof(struct scst_dgrp_dev_params, name), spdk_json_decode_string},
	{"dev_name", offsetof(struct scst_dgrp_dev_params, dev_name), spdk_json_decode_string},
};

static const char *
scst_dgrp_dev_mgmt_file_path(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->name, "devices", SCST_MGMT_IO);
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

static struct sto_write_req_params dgrp_add_dev_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_dev_decoders,
					   scst_dgrp_dev_params_alloc, scst_dgrp_dev_params_free),
	.constructor = {
		.file_path = scst_dgrp_dev_mgmt_file_path,
		.data = scst_dgrp_add_dev_data,
	}
};

static struct sto_write_req_params dgrp_del_dev_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_dev_decoders,
					   scst_dgrp_dev_params_alloc, scst_dgrp_dev_params_free),
	.constructor = {
		.file_path = scst_dgrp_dev_mgmt_file_path,
		.data = scst_dgrp_del_dev_data,
	}
};

struct scst_tgrp_params {
	char *name;
	char *dgrp_name;
};

static void *
scst_tgrp_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_tgrp_params));
}

static void
scst_tgrp_params_free(void *arg)
{
	struct scst_tgrp_params *params = arg;

	free(params->name);
	free(params->dgrp_name);
	free(params);
}

static const struct spdk_json_object_decoder scst_tgrp_decoders[] = {
	{"name", offsetof(struct scst_tgrp_params, name), spdk_json_decode_string},
	{"dgrp_name", offsetof(struct scst_tgrp_params, dgrp_name), spdk_json_decode_string},
};

static const char *
scst_tgrp_mgmt_file_path(void *arg)
{
	struct scst_tgrp_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->dgrp_name, "target_groups", SCST_MGMT_IO);
}

static char *
scst_tgrp_add_data(void *arg)
{
	struct scst_tgrp_params *params = arg;
	return spdk_sprintf_alloc("add %s", params->name);
}

static char *
scst_tgrp_del_data(void *arg)
{
	struct scst_tgrp_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->name);
}

static struct sto_write_req_params tgrp_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_decoders,
					   scst_tgrp_params_alloc, scst_tgrp_params_free),
	.constructor = {
		.file_path = scst_tgrp_mgmt_file_path,
		.data = scst_tgrp_add_data,
	}
};

static struct sto_write_req_params tgrp_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_decoders,
					   scst_tgrp_params_alloc, scst_tgrp_params_free),
	.constructor = {
		.file_path = scst_tgrp_mgmt_file_path,
		.data = scst_tgrp_del_data,
	}
};

struct scst_tgrp_list_params {
	char *dgrp;
};

static void *
scst_tgrp_list_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_tgrp_list_params));
}

static void
scst_tgrp_list_params_free(void *arg)
{
	struct scst_tgrp_list_params *params = arg;

	free(params->dgrp);
	free(params);
}

static const struct spdk_json_object_decoder scst_tgrp_list_decoders[] = {
	{"dgrp", offsetof(struct scst_tgrp_list_params, dgrp), spdk_json_decode_string},
};

static const char *
scst_tgrp_list_name(void *arg)
{
	return spdk_sprintf_alloc("Target Groups");
}

static char *
scst_tgrp_list_dirpath(void *arg)
{
	struct scst_tgrp_list_params *params = arg;

	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->dgrp, "target_groups");
}

static int
scst_tgrp_list_exclude(const char **exclude_list)
{
	exclude_list[0] = SCST_MGMT_IO;

	return 0;
}

static struct sto_ls_req_params tgrp_list_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_list_decoders,
					   scst_tgrp_list_params_alloc, scst_tgrp_list_params_free),
	.constructor = {
		.name = scst_tgrp_list_name,
		.dirpath = scst_tgrp_list_dirpath,
		.exclude = scst_tgrp_list_exclude,
	}
};

struct scst_tgrp_tgt_params {
	char *tgt_name;
	char *dgrp_name;
	char *tgrp_name;
};

static void *
scst_tgrp_tgt_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_tgrp_tgt_params));
}

static void
scst_tgrp_tgt_params_free(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;

	free(params->tgt_name);
	free(params->dgrp_name);
	free(params->tgrp_name);
	free(params);
}

static const struct spdk_json_object_decoder scst_tgrp_tgt_decoders[] = {
	{"tgt_name", offsetof(struct scst_tgrp_tgt_params, tgt_name), spdk_json_decode_string},
	{"dgrp_name", offsetof(struct scst_tgrp_tgt_params, dgrp_name), spdk_json_decode_string},
	{"tgrp_name", offsetof(struct scst_tgrp_tgt_params, tgrp_name), spdk_json_decode_string},
};

static const char *
scst_tgrp_tgt_mgmt_file_path(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->dgrp_name, "target_groups",
				  params->tgrp_name, SCST_MGMT_IO);
}

static char *
scst_tgrp_add_tgt_data(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;
	return spdk_sprintf_alloc("add %s", params->tgt_name);
}

static char *
scst_tgrp_del_tgt_data(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->tgt_name);
}

static struct sto_write_req_params tgrp_add_tgt_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_tgt_decoders,
					   scst_tgrp_tgt_params_alloc, scst_tgrp_tgt_params_free),
	.constructor = {
		.file_path = scst_tgrp_tgt_mgmt_file_path,
		.data = scst_tgrp_add_tgt_data,
	}
};

static struct sto_write_req_params tgrp_del_tgt_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_tgt_decoders,
					   scst_tgrp_tgt_params_alloc, scst_tgrp_tgt_params_free),
	.constructor = {
		.file_path = scst_tgrp_tgt_mgmt_file_path,
		.data = scst_tgrp_del_tgt_data,
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

static struct sto_write_req_params target_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.constructor = {
		.file_path = scst_target_mgmt_file_path,
		.data = scst_target_add_data,
	}
};

static struct sto_write_req_params target_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.constructor = {
		.file_path = scst_target_mgmt_file_path,
		.data = scst_target_del_data,
	}
};

static const char *
scst_target_enable_file_path(void *arg)
{
	struct scst_target_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  params->driver, params->target, "enabled");
}

static char *
scst_target_enable_data(void *arg)
{
	return spdk_sprintf_alloc("1");
}

static char *
scst_target_disable_data(void *arg)
{
	return spdk_sprintf_alloc("0");
}

static struct sto_write_req_params target_enable_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.constructor = {
		.file_path = scst_target_enable_file_path,
		.data = scst_target_enable_data,
	}
};

static struct sto_write_req_params target_disable_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.constructor = {
		.file_path = scst_target_enable_file_path,
		.data = scst_target_disable_data,
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

static struct sto_write_req_params group_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_group_decoders,
					   scst_group_params_alloc, scst_group_params_free),
	.constructor = {
		.file_path = scst_group_mgmt_file_path,
		.data = scst_group_add_data,
	}
};

static struct sto_write_req_params group_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_group_decoders,
					   scst_group_params_alloc, scst_group_params_free),
	.constructor = {
		.file_path = scst_group_mgmt_file_path,
		.data = scst_group_del_data,
	}
};

static const char *
scst_lun_mgmt_file_constructor(char *driver, char *target, char *group)
{
	if (group) {
		return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s/%s/%s", SCST_ROOT,
					  SCST_TARGETS, driver,
					  target, "ini_groups", group,
					  "luns", SCST_MGMT_IO);
	}

	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT,
				  SCST_TARGETS, driver,
				  target, "luns", SCST_MGMT_IO);
}

struct scst_lun_add_params {
	int lun;
	char *driver;
	char *target;
	char *device;
	char *group;
	struct scst_attr_name_list attr_list;
};

static void *
scst_lun_add_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_lun_add_params));
}

static void
scst_lun_add_params_free(void *arg)
{
	struct scst_lun_add_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->device);
	free(params->group);
	scst_attr_list_free(&params->attr_list);
	free(params);
}

static const struct spdk_json_object_decoder scst_lun_add_decoders[] = {
	{"lun", offsetof(struct scst_lun_add_params, lun), spdk_json_decode_int32},
	{"driver", offsetof(struct scst_lun_add_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_add_params, target), spdk_json_decode_string},
	{"device", offsetof(struct scst_lun_add_params, device), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_add_params, group), spdk_json_decode_string, true},
	{"attributes", offsetof(struct scst_lun_add_params, attr_list), scst_attr_list_decode, true},
};

static const char *
scst_lun_add_mgmt_file_path(void *arg)
{
	struct scst_lun_add_params *params = arg;

	return scst_lun_mgmt_file_constructor(params->driver, params->target, params->group);
}

static char *
scst_lun_add_data(void *arg)
{
	struct scst_lun_add_params *params = arg;
	char *data;
	int rc;

	data = spdk_sprintf_alloc("add %s %d", params->device, params->lun);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return NULL;
	}

	rc = scst_attr_list_fill_data(&params->attr_list, &data);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to fill scst attrs\n");
		free(data);
		return NULL;
	}

	return data;
}

static struct sto_write_req_params lun_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_lun_add_decoders,
					   scst_lun_add_params_alloc, scst_lun_add_params_free),
	.constructor = {
		.file_path = scst_lun_add_mgmt_file_path,
		.data = scst_lun_add_data,
	}
};

struct scst_lun_del_params {
	int lun;
	char *driver;
	char *target;
	char *group;
};

static void *
scst_lun_del_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_lun_del_params));
}

static void
scst_lun_del_params_free(void *arg)
{
	struct scst_lun_del_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->group);
	free(params);
}

static const struct spdk_json_object_decoder scst_lun_del_decoders[] = {
	{"lun", offsetof(struct scst_lun_add_params, lun), spdk_json_decode_int32},
	{"driver", offsetof(struct scst_lun_del_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_del_params, target), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_del_params, group), spdk_json_decode_string, true},
};

static const char *
scst_lun_del_mgmt_file_path(void *arg)
{
	struct scst_lun_del_params *params = arg;

	return scst_lun_mgmt_file_constructor(params->driver, params->target, params->group);
}

static char *
scst_lun_del_data(void *arg)
{
	struct scst_lun_del_params *params = arg;

	return spdk_sprintf_alloc("del %d", params->lun);
}

static struct sto_write_req_params lun_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_lun_del_decoders,
					   scst_lun_del_params_alloc, scst_lun_del_params_free),
	.constructor = {
		.file_path = scst_lun_del_mgmt_file_path,
		.data = scst_lun_del_data,
	}
};

static char *
scst_lun_replace_data(void *arg)
{
	struct scst_lun_add_params *params = arg;
	char *data;
	int rc;

	data = spdk_sprintf_alloc("replace %s %d", params->device, params->lun);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return NULL;
	}

	rc = scst_attr_list_fill_data(&params->attr_list, &data);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to fill scst attrs\n");
		free(data);
		return NULL;
	}

	return data;
}

static struct sto_write_req_params lun_replace_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_lun_add_decoders,
					   scst_lun_add_params_alloc, scst_lun_add_params_free),
	.constructor = {
		.file_path = scst_lun_add_mgmt_file_path,
		.data = scst_lun_replace_data,
	}
};

struct scst_lun_clear_params {
	char *driver;
	char *target;
	char *group;
};

static void *
scst_lun_clear_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_lun_clear_params));
}

static void
scst_lun_clear_params_free(void *arg)
{
	struct scst_lun_clear_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->group);
	free(params);
}

static const struct spdk_json_object_decoder scst_lun_clear_decoders[] = {
	{"driver", offsetof(struct scst_lun_clear_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_clear_params, target), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_clear_params, group), spdk_json_decode_string, true},
};

static const char *
scst_lun_clear_mgmt_file_path(void *arg)
{
	struct scst_lun_clear_params *params = arg;

	return scst_lun_mgmt_file_constructor(params->driver, params->target, params->group);
}

static char *
scst_lun_clear_data(void *arg)
{
	return spdk_sprintf_alloc("clear");
}

static struct sto_write_req_params lun_clear_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_lun_clear_decoders,
					   scst_lun_clear_params_alloc, scst_lun_clear_params_free),
	.constructor = {
		.file_path = scst_lun_clear_mgmt_file_path,
		.data = scst_lun_clear_data,
	}
};

static const struct sto_cdbops scst_op_table[] = {
	{
		.name = "handler_list",
		.req_constructor = sto_ls_req_constructor,
		.req_ops = &sto_ls_req_ops,
		.params_constructor = &handler_list_constructor,
	},
	{
		.name = "driver_list",
		.req_constructor = sto_ls_req_constructor,
		.req_ops = &sto_ls_req_ops,
		.params_constructor = &driver_list_constructor,
	},
	{
		.name = "dev_open",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dev_open_constructor,
	},
	{
		.name = "dev_close",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dev_close_constructor,
	},
	{
		.name = "dev_resync",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dev_resync_constructor,
	},
	{
		.name = "dev_list",
		.req_constructor = sto_ls_req_constructor,
		.req_ops = &sto_ls_req_ops,
		.params_constructor = &dev_list_constructor,
	},
	{
		.name = "dgrp_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dgrp_add_constructor,
	},
	{
		.name = "dgrp_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dgrp_del_constructor,
	},
	{
		.name = "dgrp_list",
		.req_constructor = sto_ls_req_constructor,
		.req_ops = &sto_ls_req_ops,
		.params_constructor = &dgrp_list_constructor,
	},
	{
		.name = "dgrp_add_dev",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dgrp_add_dev_constructor,
	},
	{
		.name = "dgrp_del_dev",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dgrp_del_dev_constructor,
	},
	{
		.name = "tgrp_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &tgrp_add_constructor,
	},
	{
		.name = "tgrp_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &tgrp_del_constructor,
	},
	{
		.name = "tgrp_list",
		.req_constructor = sto_ls_req_constructor,
		.req_ops = &sto_ls_req_ops,
		.params_constructor = &tgrp_list_constructor,
	},
	{
		.name = "tgrp_add_tgt",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &tgrp_add_tgt_constructor,
	},
	{
		.name = "tgrp_del_tgt",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &tgrp_del_tgt_constructor,
	},
	{
		.name = "target_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &target_add_constructor,
	},
	{
		.name = "target_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &target_del_constructor,
	},
	{
		.name = "target_list",
		.req_constructor = scst_tg_list_req_constructor,
		.req_ops = &scst_tg_list_req_ops,
	},
	{
		.name = "target_enable",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &target_enable_constructor,
	},
	{
		.name = "target_disable",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &target_disable_constructor,
	},
	{
		.name = "group_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &group_add_constructor,
	},
	{
		.name = "group_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &group_del_constructor,
	},
	{
		.name = "lun_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &lun_add_constructor,
	},
	{
		.name = "lun_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &lun_del_constructor,
	},
	{
		.name = "lun_replace",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &lun_replace_constructor,
	},
	{
		.name = "lun_clear",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &lun_clear_constructor,
	},
};

#define SCST_OP_TBL_SIZE	(SPDK_COUNTOF(scst_op_table))

static const struct sto_cdbops *
scst_find_cdbops(const char *op_name)
{
	int i;

	for (i = 0; i < SCST_OP_TBL_SIZE; i++) {
		const struct sto_cdbops *op = &scst_op_table[i];

		if (!strcmp(op_name, op->name)) {
			return op;
		}
	}

	return NULL;
}

static struct sto_subsystem g_scst_subsystem = {
	.name = "scst",
	.find_cdbops = scst_find_cdbops,
};

STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
