#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "sto_aio_front.h"
#include "err.h"

int
sto_decoder_parse(struct sto_decoder *decoder, const struct spdk_json_val *data,
		  sto_params_parse params_parse, void *priv)
{
	void *params;
	int rc = 0;

	params = decoder->params_alloc();
	if (spdk_unlikely(!params)) {
		SPDK_ERRLOG("Failed to alloc params\n");
		return -ENOMEM;
	}

	if (spdk_json_decode_object(data, decoder->decoders, decoder->num_decoders, params)) {
		SPDK_ERRLOG("Failed to decode params\n");
		rc = -EINVAL;
		goto out;
	}

	rc = params_parse(priv, params);

out:
	decoder->params_free(params);

	return rc;
}

void
sto_err(struct sto_err_context *err, int rc)
{
	err->rc = rc;
	err->errno_msg = spdk_strerror(-rc);
}

int
sto_decode_object_str(const struct spdk_json_val *values,
		      const char *name, char **value)
{
	const struct spdk_json_val *name_json, *value_json;

	if (!values || values->type != SPDK_JSON_VAL_OBJECT_BEGIN || !values->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", values);
		return -EINVAL;
	}

	name_json = &values[1];
	if (!spdk_json_strequal(name_json, name)) {
		SPDK_ERRLOG("JSON object name doesn't correspond to %s\n", name);
		return -ENOENT;
	}

	value_json = &values[2];

	if (spdk_json_decode_string(value_json, value)) {
		SPDK_ERRLOG("Failed to decode string from JSON object %s\n", name);
		return -EDOM;
	}

	return 0;
}

static int
sto_decode_value_len(const struct spdk_json_val *values)
{
	const struct spdk_json_val *value_json;

	if (!values || values->type != SPDK_JSON_VAL_OBJECT_BEGIN || !values->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", values);
		return -EINVAL;
	}

	value_json = &values[2];

	return 1 + spdk_json_val_len(value_json);
}

const struct spdk_json_val *
sto_decode_next_cdb(const struct spdk_json_val *params)
{
	struct spdk_json_val *cdb;
	uint32_t cdb_len, size;
	int val_len = 0;
	int i;

	if (!params || params->type != SPDK_JSON_VAL_OBJECT_BEGIN || !params->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", params);
		return ERR_PTR(-EINVAL);
	}

	SPDK_NOTICELOG("Start parse JSON for CDB: params_len=%u\n", params->len);

	val_len = sto_decode_value_len(params);
	if (val_len < 0) {
		SPDK_ERRLOG("Failed to decode CDB\n");
		return ERR_PTR(val_len);
	}

	cdb_len = params->len - val_len;
	if (!cdb_len) {
		SPDK_NOTICELOG("CDB len is equal zero: val_len=%u params_len=%u\n",
			       val_len, params->len);
		return NULL;
	}

	size = cdb_len + 2;

	cdb = calloc(size, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!cdb)) {
		SPDK_ERRLOG("Failed to alloc CDB: size=%u\n", size);
		return ERR_PTR(-ENOMEM);
	}

	cdb->type = SPDK_JSON_VAL_OBJECT_BEGIN;
	cdb->len = cdb_len;

	for (i = 1; i <= cdb_len + 1; i++) {
		cdb[i].start = params[i + val_len].start;
		cdb[i].len = params[i + val_len].len;
		cdb[i].type = params[i + val_len].type;
	}

	return cdb;
}

void
sto_status_ok(struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "status", "OK");

	spdk_json_write_object_end(w);
}

void
sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "status", "FAILED");
	spdk_json_write_named_int32(w, "error", err->rc);
	spdk_json_write_named_string(w, "msg", err->errno_msg);

	spdk_json_write_object_end(w);
}

struct sto_req *
sto_write_req_constructor(const struct sto_cdbops *op)
{
	struct sto_write_req *write_req;

	write_req = rte_zmalloc(NULL, sizeof(*write_req), 0);
	if (spdk_unlikely(!write_req)) {
		SPDK_ERRLOG("Failed to alloc sto write req\n");
		return NULL;
	}

	sto_req_init(&write_req->req, op);

	return &write_req->req;
}

static int
sto_write_req_params_parse(void *priv, void *params)
{
	struct sto_write_req_params *p = priv;
	struct sto_write_req *req = p->req;
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
sto_write_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_write_req *write_req = to_write_req(req);
	struct sto_write_req_params *p = req->params_constructor;
	int rc = 0;

	p->req = write_req;

	rc = sto_decoder_parse(&p->decoder, cdb, sto_write_req_params_parse, p);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to parse CDB\n");
	}

	return rc;
}

static void
sto_write_req_done(struct sto_aio *aio)
{
	struct sto_req *req = aio->priv;
	int rc;

	rc = aio->returncode;

	sto_aio_free(aio);

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to device group add\n");
		sto_err(req->ctx.err_ctx, rc);
	}

	sto_req_response(req);
}

static int
sto_write_req_exec(struct sto_req *req)
{
	struct sto_write_req *write_req = to_write_req(req);

	return sto_aio_write_string(write_req->file, write_req->data,
				    sto_write_req_done, req);
}

static void
sto_write_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

static void
sto_write_req_free(struct sto_req *req)
{
	struct sto_write_req *write_req = to_write_req(req);

	free((char *) write_req->file);
	free(write_req->data);

	rte_free(write_req);
}

struct sto_req_ops sto_write_req_ops = {
	.decode_cdb = sto_write_req_decode_cdb,
	.exec = sto_write_req_exec,
	.end_response = sto_write_req_end_response,
	.free = sto_write_req_free,
};

struct sto_req *
sto_ls_req_constructor(const struct sto_cdbops *op)
{
	struct sto_ls_req *ls_req;

	ls_req = rte_zmalloc(NULL, sizeof(*ls_req), 0);
	if (spdk_unlikely(!ls_req)) {
		SPDK_ERRLOG("Failed to alloc sto ls req\n");
		return NULL;
	}

	sto_req_init(&ls_req->req, op);

	return &ls_req->req;
}

static int
sto_ls_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_ls_req *ls_req = to_ls_req(req);
	struct sto_ls_req_params *p = req->params_constructor;

	ls_req->name = p->constructor.name();
	if (spdk_unlikely(!ls_req->name)) {
		SPDK_ERRLOG("Failed to alloc memory for targets\n");
		return -ENOMEM;
	}

	ls_req->dirpath = p->constructor.dirpath();
	if (spdk_unlikely(!ls_req->dirpath)) {
		SPDK_ERRLOG("Failed to alloc memory for dirpath\n");
		goto free_name;
	}

	if (p->constructor.exclude) {
		int rc;

		rc = p->constructor.exclude(ls_req->exclude_list);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to init exclude list, rc=%d\n", rc);
			goto free_dirpath;
		}
	}

	return 0;

free_dirpath:
	free(ls_req->dirpath);

free_name:
	free((char *) ls_req->name);

	return -ENOMEM;
}

static void
sto_ls_req_done(void *priv)
{
	struct sto_req *req = priv;
	struct sto_ls_req *ls_req = to_ls_req(req);
	struct sto_readdir_result *result = &ls_req->result;
	int rc;

	rc = result->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to list directories\n");
		sto_err(req->ctx.err_ctx, rc);
		goto out;
	}

out:
	sto_req_response(req);
}

static int
sto_ls_req_exec(struct sto_req *req)
{
	struct sto_ls_req *ls_req = to_ls_req(req);
	struct sto_readdir_args args = {
		.priv = req,
		.readdir_done = sto_ls_req_done,
		.result = &ls_req->result,
	};

	return sto_readdir(ls_req->dirpath, &args);
}

static void
sto_ls_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_ls_req *ls_req = to_ls_req(req);
	struct sto_readdir_result *result = &ls_req->result;
	struct sto_dirents_json_cfg cfg = {
		.name = ls_req->name,
		.exclude_list = ls_req->exclude_list,
	};

	sto_dirents_info_json(&result->dirents, &cfg, w);
}

static void
sto_ls_req_free(struct sto_req *req)
{
	struct sto_ls_req *ls_req = to_ls_req(req);

	free((char *) ls_req->name);
	free(ls_req->dirpath);

	sto_readdir_result_free(&ls_req->result);

	rte_free(ls_req);
}

struct sto_req_ops sto_ls_req_ops = {
	.decode_cdb = sto_ls_req_decode_cdb,
	.exec = sto_ls_req_exec,
	.end_response = sto_ls_req_end_response,
	.free = sto_ls_req_free,
};
