#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "sto_rpc_aio.h"
#include "err.h"

int
sto_decoder_parse(struct sto_decoder *decoder, const struct spdk_json_val *data,
		  sto_params_parse params_parse, void *priv)
{
	void *params;
	int rc = 0;

	if (!decoder->initialized || (!data && decoder->allow_empty)) {
		return params_parse(priv, NULL);
	}

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

STO_REQ_CONSTRUCTOR_DEFINE(write)

static void
sto_write_req_params_free(struct sto_write_req_params *params)
{
	free((char *) params->file);
	free(params->data);
}

static int
sto_write_req_params_parse(void *priv, void *params)
{
	struct sto_write_req_params_constructor *constructor = priv;
	struct sto_write_req_params *p = constructor->inner.params;

	p->file = constructor->file_path(params);
	if (spdk_unlikely(!p->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		return -ENOMEM;
	}

	p->data = constructor->data(params);
	if (spdk_unlikely(!p->data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		goto free_file;
	}

	return 0;

free_file:
	free((char *) p->file);

	return -ENOMEM;
}

static int
sto_write_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_write_req *write_req = STO_REQ_TYPE(req, write);
	struct sto_write_req_params_constructor *constructor = req->params_constructor;
	int rc = 0;

	constructor->inner.params = &write_req->params;

	rc = sto_decoder_parse(&constructor->decoder, cdb, sto_write_req_params_parse, constructor);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to parse params for write req\n");
	}

	return rc;
}

static void
sto_write_req_done(void *priv, int rc)
{
	struct sto_req *req = priv;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("WRITE req failed, rc=%d\n", rc);
		sto_err(req->ctx.err_ctx, rc);
	}

	sto_req_response(req);
}

static int
sto_write_req_exec(struct sto_req *req)
{
	struct sto_write_req *write_req = STO_REQ_TYPE(req, write);
	struct sto_write_req_params *params = &write_req->params;
	struct sto_rpc_writefile_args args = {
		.priv = req,
		.done = sto_write_req_done,
	};

	return sto_rpc_writefile(params->file, params->data, &args);
}

static void
sto_write_req_free(struct sto_req *req)
{
	struct sto_write_req *write_req = STO_REQ_TYPE(req, write);

	sto_write_req_params_free(&write_req->params);

	rte_free(write_req);
}

struct sto_req_ops sto_write_req_ops = {
	.decode_cdb = sto_write_req_decode_cdb,
	.exec = sto_write_req_exec,
	.end_response = sto_dummy_req_end_response,
	.free = sto_write_req_free,
};

STO_REQ_CONSTRUCTOR_DEFINE(read)

static void
sto_read_req_params_free(struct sto_read_req_params *params)
{
	free((char *) params->file);
}

static int
sto_read_req_params_parse(void *priv, void *params)
{
	struct sto_read_req_params_constructor *constructor = priv;
	struct sto_read_req_params *p = constructor->inner.params;

	p->file = constructor->file_path(params);
	if (spdk_unlikely(!p->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		return -ENOMEM;
	}

	if (constructor->size) {
		p->size = constructor->size(params);
	}

	return 0;
}

static int
sto_read_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_read_req *read_req = STO_REQ_TYPE(req, read);
	struct sto_read_req_params_constructor *constructor = req->params_constructor;
	int rc = 0;

	constructor->inner.params = &read_req->params;

	rc = sto_decoder_parse(&constructor->decoder, cdb, sto_read_req_params_parse, constructor);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to parse params for read req\n");
	}

	return rc;
}

static void
sto_read_req_done(void *priv, int rc)
{
	struct sto_req *req = priv;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("READ req failed, rc=%d\n", rc);
		sto_err(req->ctx.err_ctx, rc);
	}

	sto_req_response(req);
}

static int
sto_read_req_exec(struct sto_req *req)
{
	struct sto_read_req *read_req = STO_REQ_TYPE(req, read);
	struct sto_read_req_params *params = &read_req->params;
	struct sto_rpc_readfile_args args = {
		.priv = req,
		.done = sto_read_req_done,
		.buf = &read_req->buf,
	};

	return sto_rpc_readfile(params->file, params->size, &args);
}

static void
sto_read_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_read_req *read_req = STO_REQ_TYPE(req, read);

	spdk_json_write_string(w, read_req->buf);
}

static void
sto_read_req_free(struct sto_req *req)
{
	struct sto_read_req *read_req = STO_REQ_TYPE(req, read);

	sto_read_req_params_free(&read_req->params);
	free(read_req->buf);

	rte_free(read_req);
}

struct sto_req_ops sto_read_req_ops = {
	.decode_cdb = sto_read_req_decode_cdb,
	.exec = sto_read_req_exec,
	.end_response = sto_read_req_end_response,
	.free = sto_read_req_free,
};

STO_REQ_CONSTRUCTOR_DEFINE(readlink)

static void
sto_readlink_req_params_free(struct sto_readlink_req_params *params)
{
	free((char *) params->file);
}

static int
sto_readlink_req_params_parse(void *priv, void *params)
{
	struct sto_readlink_req_params_constructor *constructor = priv;
	struct sto_readlink_req_params *p = constructor->inner.params;

	p->file = constructor->file_path(params);
	if (spdk_unlikely(!p->file)) {
		SPDK_ERRLOG("Failed to alloc memory for file path\n");
		return -ENOMEM;
	}

	return 0;
}

static int
sto_readlink_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_readlink_req *readlink_req = STO_REQ_TYPE(req, readlink);
	struct sto_readlink_req_params_constructor *constructor = req->params_constructor;
	int rc = 0;

	constructor->inner.params = &readlink_req->params;

	rc = sto_decoder_parse(&constructor->decoder, cdb, sto_readlink_req_params_parse, constructor);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to parse params for readlink req\n");
	}

	return rc;
}

static void
sto_readlink_req_done(void *priv, int rc)
{
	struct sto_req *req = priv;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("READ req failed, rc=%d\n", rc);
		sto_err(req->ctx.err_ctx, rc);
	}

	sto_req_response(req);
}

static int
sto_readlink_req_exec(struct sto_req *req)
{
	struct sto_readlink_req *readlink_req = STO_REQ_TYPE(req, readlink);
	struct sto_readlink_req_params *params = &readlink_req->params;
	struct sto_rpc_readlink_args args = {
		.priv = req,
		.done = sto_readlink_req_done,
		.buf = &readlink_req->buf,
	};

	return sto_rpc_readlink(params->file, &args);
}

static void
sto_readlink_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_readlink_req *readlink_req = STO_REQ_TYPE(req, readlink);

	spdk_json_write_string(w, readlink_req->buf);
}

static void
sto_readlink_req_free(struct sto_req *req)
{
	struct sto_readlink_req *readlink_req = STO_REQ_TYPE(req, readlink);

	sto_readlink_req_params_free(&readlink_req->params);
	free(readlink_req->buf);

	rte_free(readlink_req);
}

struct sto_req_ops sto_readlink_req_ops = {
	.decode_cdb = sto_readlink_req_decode_cdb,
	.exec = sto_readlink_req_exec,
	.end_response = sto_readlink_req_end_response,
	.free = sto_readlink_req_free,
};

STO_REQ_CONSTRUCTOR_DEFINE(readdir)

static void
sto_readdir_req_params_free(struct sto_readdir_req_params *params)
{
	free((char *) params->name);
	free(params->dirpath);
}

static int
sto_readdir_req_params_parse(void *priv, void *params)
{
	struct sto_readdir_req_params_constructor *constructor = priv;
	struct sto_readdir_req_params *p = constructor->inner.params;

	p->name = constructor->name(params);
	if (spdk_unlikely(!p->name)) {
		SPDK_ERRLOG("Failed to alloc memory for name\n");
		return -ENOMEM;
	}

	p->dirpath = constructor->dirpath(params);
	if (spdk_unlikely(!p->dirpath)) {
		SPDK_ERRLOG("Failed to alloc memory for dirpath\n");
		goto free_name;
	}

	if (constructor->exclude) {
		int rc;

		rc = constructor->exclude(p->exclude_list);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to init exclude list, rc=%d\n", rc);
			goto free_dirpath;
		}
	}

	return 0;

free_dirpath:
	free(p->dirpath);

free_name:
	free((char *) p->name);

	return -ENOMEM;
}

static int
sto_readdir_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_readdir_req *readdir_req = STO_REQ_TYPE(req, readdir);
	struct sto_readdir_req_params_constructor *constructor = req->params_constructor;
	int rc = 0;

	constructor->inner.params = &readdir_req->params;

	rc = sto_decoder_parse(&constructor->decoder, cdb, sto_readdir_req_params_parse, constructor);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to parse params for ls req\n");
	}

	return rc;
}

static void
sto_readdir_req_done(void *priv, int rc)
{
	struct sto_req *req = priv;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to list directories\n");
		sto_err(req->ctx.err_ctx, rc);
		goto out;
	}

out:
	sto_req_response(req);
}

static int
sto_readdir_req_exec(struct sto_req *req)
{
	struct sto_readdir_req *readdir_req = STO_REQ_TYPE(req, readdir);
	struct sto_readdir_req_params *params = &readdir_req->params;
	struct sto_rpc_readdir_args args = {
		.priv = req,
		.done = sto_readdir_req_done,
		.dirents = &readdir_req->dirents,
	};

	return sto_rpc_readdir(params->dirpath, &args);
}

static void
sto_readdir_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_readdir_req *readdir_req = STO_REQ_TYPE(req, readdir);
	struct sto_readdir_req_params *params = &readdir_req->params;
	struct sto_dirents_json_cfg cfg = {
		.name = params->name,
		.exclude_list = params->exclude_list,
	};

	sto_dirents_info_json(&readdir_req->dirents, &cfg, w);
}

static void
sto_readdir_req_free(struct sto_req *req)
{
	struct sto_readdir_req *readdir_req = STO_REQ_TYPE(req, readdir);

	sto_readdir_req_params_free(&readdir_req->params);

	sto_dirents_free(&readdir_req->dirents);

	rte_free(readdir_req);
}

struct sto_req_ops sto_readdir_req_ops = {
	.decode_cdb = sto_readdir_req_decode_cdb,
	.exec = sto_readdir_req_exec,
	.end_response = sto_readdir_req_end_response,
	.free = sto_readdir_req_free,
};

STO_REQ_CONSTRUCTOR_DEFINE(tree)

static void
sto_tree_req_params_free(struct sto_tree_req_params *params)
{
	free(params->dirpath);
}

static int
sto_tree_req_params_parse(void *priv, void *params)
{
	struct sto_tree_req_params_constructor *constructor = priv;
	struct sto_tree_req_params *p = constructor->inner.params;

	p->dirpath = constructor->dirpath(params);
	if (spdk_unlikely(!p->dirpath)) {
		SPDK_ERRLOG("Failed to alloc memory for dirpath\n");
		return -ENOMEM;
	}

	if (constructor->depth) {
		p->depth = constructor->depth(params);
	}

	if (constructor->only_dirs) {
		p->only_dirs = constructor->only_dirs(params);
	}

	if (constructor->info_json) {
		p->info_json = constructor->info_json;
	}

	return 0;
}

static int
sto_tree_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_tree_req *tree_req = STO_REQ_TYPE(req, tree);
	struct sto_tree_req_params_constructor *constructor = req->params_constructor;
	int rc = 0;

	constructor->inner.params = &tree_req->params;

	rc = sto_decoder_parse(&constructor->decoder, cdb, sto_tree_req_params_parse, constructor);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to parse params for tree req\n");
	}

	return rc;
}

static void
sto_tree_req_done(void *priv)
{
	struct sto_req *req = priv;
	struct sto_tree_req *tree_req = STO_REQ_TYPE(req, tree);
	struct sto_tree_info *info = &tree_req->info;
	int rc;

	rc = info->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to tree\n");
		sto_err(req->ctx.err_ctx, rc);
		goto out;
	}

out:
	sto_req_response(req);
}

static int
sto_tree_req_exec(struct sto_req *req)
{
	struct sto_tree_req *tree_req = STO_REQ_TYPE(req, tree);
	struct sto_tree_req_params *params = &tree_req->params;
	struct sto_tree_args args = {
		.priv = req,
		.done = sto_tree_req_done,
		.info = &tree_req->info,
	};

	return sto_tree(params->dirpath, params->depth, params->only_dirs, &args);
}

static void
sto_tree_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_tree_req *tree_req = STO_REQ_TYPE(req, tree);
	struct sto_tree_info *info = &tree_req->info;
	struct sto_tree_req_params *params = &tree_req->params;

	if (params->info_json) {
		params->info_json(&info->tree_root, w);
		return;
	}

	sto_tree_info_json(&info->tree_root, w);
}

static void
sto_tree_req_free(struct sto_req *req)
{
	struct sto_tree_req *tree_req = STO_REQ_TYPE(req, tree);

	sto_tree_req_params_free(&tree_req->params);
	sto_tree_info_free(&tree_req->info);

	rte_free(tree_req);
}

struct sto_req_ops sto_tree_req_ops = {
	.decode_cdb = sto_tree_req_decode_cdb,
	.exec = sto_tree_req_exec,
	.end_response = sto_tree_req_end_response,
	.free = sto_tree_req_free,
};
