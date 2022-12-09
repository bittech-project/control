#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "sto_rpc_aio.h"
#include "err.h"

#define STO_EXEC_POLL_PERIOD		1000 /* 1ms */
#define STO_ROLLBACK_POLL_PERIOD	1000 /* 1ms */

static struct spdk_poller *g_sto_exec_poller;
static struct spdk_poller *g_sto_rollback_poller;

TAILQ_HEAD(sto_req_list, sto_req);

static struct sto_req_list g_sto_req_exec_list = TAILQ_HEAD_INITIALIZER(g_sto_req_exec_list);
static struct sto_req_list g_sto_req_rollback_list = TAILQ_HEAD_INITIALIZER(g_sto_req_rollback_list);


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

static struct sto_exec_ctx *
sto_exec_ctx_alloc(sto_req_exec_t exec_fn)
{
	struct sto_exec_ctx *ctx;

	ctx = rte_zmalloc(NULL, sizeof(*ctx), 0);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc exec context\n");
		return NULL;
	}

	ctx->exec_fn = exec_fn;

	return ctx;
}

static void
sto_exec_ctx_free(struct sto_exec_ctx *ctx)
{
	rte_free(ctx);
}

static int
sto_req_add_rollback(struct sto_req *req, sto_req_exec_t rollback_fn)
{
	struct sto_exec_ctx *ctx;

	ctx = sto_exec_ctx_alloc(rollback_fn);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to allocate rollback context\n");
		return -ENOMEM;
	}

	TAILQ_INSERT_HEAD(&req->rollback_stack, ctx, list);

	return 0;
}

int
sto_req_add_exec(struct sto_req *req, sto_req_exec_t exec_fn, sto_req_exec_t rollback_fn)
{
	struct sto_exec_ctx *ctx;
	int rc = 0;

	ctx = sto_exec_ctx_alloc(exec_fn);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to allocate exec context\n");
		return -ENOMEM;
	}

	TAILQ_INSERT_TAIL(&req->exe_queue, ctx, list);

	if (rollback_fn) {
		rc = sto_req_add_rollback(req, rollback_fn);
		if (spdk_unlikely(rc)) {
			TAILQ_REMOVE(&req->exe_queue, ctx, list);
			sto_exec_ctx_free(ctx);
			return rc;
		}
	}

	return 0;
}

int
sto_req_add_exec_entries(struct sto_req *req, const struct sto_exec_entry *entries, size_t size)
{
	int i, rc;

	for (i = 0; i < size; i++) {
		rc = sto_req_add_exec(req, entries[i].exec_fn, entries[i].rollback_fn);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to add %d exec\n", i);
			return rc;
		}
	}

	return 0;
}

static sto_req_exec_t
sto_exec_list_get_first(struct sto_exec_list *list)
{
	struct sto_exec_ctx *ctx;
	sto_req_exec_t exec_fn;

	ctx = TAILQ_FIRST(list);
	if (!ctx) {
		return NULL;
	}

	TAILQ_REMOVE(list, ctx, list);

	exec_fn = ctx->exec_fn;

	sto_exec_ctx_free(ctx);

	return exec_fn;
}

static void
sto_exec_list_free(struct sto_exec_list *list)
{
	struct sto_exec_ctx *ctx, *tmp;

	TAILQ_FOREACH_SAFE(ctx, list, list, tmp) {
		TAILQ_REMOVE(list, ctx, list);

		sto_exec_ctx_free(ctx);
	}
}

void
sto_req_free(struct sto_req *req)
{
	sto_exec_list_free(&req->exe_queue);
	sto_exec_list_free(&req->rollback_stack);
}

static void
sto_req_rollback(struct sto_req *req)
{
	sto_req_exec_t rollback_fn;
	int rc = 0;

	if (spdk_unlikely(req->returncode)) {
		SPDK_ERRLOG("Rollback has been failed, rc=%d\n",
			    req->returncode);
		goto out_response;
	}

	rollback_fn = sto_exec_list_get_first(&req->rollback_stack);
	if (!rollback_fn) {
		goto out_response;
	}

	rc = rollback_fn(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to rollback, rc=%d\n", rc);
		goto out_response;
	}

	return;

out_response:
	sto_req_done(req);

	return;
}

static int
sto_rollback_poll(void *ctx)
{
	struct sto_req_list req_list = TAILQ_HEAD_INITIALIZER(req_list);
	struct sto_req *req, *tmp;

	if (likely(TAILQ_EMPTY(&g_sto_req_rollback_list))) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&req_list, &g_sto_req_rollback_list, sto_req, list);

	TAILQ_FOREACH_SAFE(req, &req_list, list, tmp) {
		TAILQ_REMOVE(&req_list, req, list);

		sto_req_rollback(req);
	}

	return SPDK_POLLER_BUSY;
}

static void
sto_req_exec_error(struct sto_req *req, int rc)
{
	sto_err(req->ctx.err_ctx, rc);

	if (!TAILQ_EMPTY(&req->rollback_stack)) {
		req->returncode = 0;
		TAILQ_INSERT_TAIL(&g_sto_req_rollback_list, req, list);

		return;
	}

	sto_req_done(req);

	return;
}

static void
sto_req_exec(struct sto_req *req)
{
	struct sto_req_ops *ops = req->ops;
	sto_req_exec_t exec_fn;
	int rc = 0;

	if (spdk_unlikely(req->returncode)) {
		rc = req->returncode;
		goto out_err;
	}

	do {
		exec_fn = sto_exec_list_get_first(&req->exe_queue);
		if (exec_fn) {
			rc = exec_fn(req);
			if (spdk_unlikely(rc)) {
				SPDK_ERRLOG("Failed to exec queue for req[%p]\n", req);
				goto out_err;
			}

			return;
		}

		rc = ops->exec_constructor(req, req->state);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to construct exec for req[%p]\n", req);
			goto out_err;
		}

		req->state++;

	} while (!TAILQ_EMPTY(&req->exe_queue));

	sto_req_done(req);

	return;

out_err:
	sto_req_exec_error(req, rc);
}

static int
sto_exec_poll(void *ctx)
{
	struct sto_req_list req_list = TAILQ_HEAD_INITIALIZER(req_list);
	struct sto_req *req, *tmp;

	if (TAILQ_EMPTY(&g_sto_req_exec_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&req_list, &g_sto_req_exec_list, sto_req, list);

	TAILQ_FOREACH_SAFE(req, &req_list, list, tmp) {
		TAILQ_REMOVE(&req_list, req, list);

		sto_req_exec(req);
	}

	return SPDK_POLLER_BUSY;
}

void
sto_req_process(struct sto_req *req, int rc)
{
	req->returncode = rc;
	TAILQ_INSERT_TAIL(&g_sto_req_exec_list, req, list);
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

static int
sto_write_req_exec(struct sto_req *req)
{
	struct sto_write_req *write_req = STO_REQ_TYPE(req, write);
	struct sto_write_req_params *params = &write_req->params;
	struct sto_rpc_writefile_args args = {
		.priv = req,
		.done = sto_req_exec_done,
	};

	return sto_rpc_writefile(params->file, params->data, &args);
}

static int
sto_write_req_exec_constructor(struct sto_req *req, int state)
{
	switch (state) {
	case 0:
		return sto_req_add_exec(req, sto_write_req_exec, NULL);
	default:
		return 0;
	}
}

static void
sto_write_req_free(struct sto_req *req)
{
	struct sto_write_req *write_req = STO_REQ_TYPE(req, write);

	sto_write_req_params_free(&write_req->params);

	sto_req_free(req);
	rte_free(write_req);
}

struct sto_req_ops sto_write_req_ops = {
	.decode_cdb = sto_write_req_decode_cdb,
	.exec_constructor = sto_write_req_exec_constructor,
	.response = sto_dummy_req_response,
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

static int
sto_read_req_exec(struct sto_req *req)
{
	struct sto_read_req *read_req = STO_REQ_TYPE(req, read);
	struct sto_read_req_params *params = &read_req->params;
	struct sto_rpc_readfile_args args = {
		.priv = req,
		.done = sto_req_exec_done,
		.buf = &read_req->buf,
	};

	return sto_rpc_readfile(params->file, params->size, &args);
}

static int
sto_read_req_exec_constructor(struct sto_req *req, int state)
{
	switch (state) {
	case 0:
		return sto_req_add_exec(req, sto_read_req_exec, NULL);
	default:
		return 0;
	}
}

static void
sto_read_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
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

	sto_req_free(req);
	rte_free(read_req);
}

struct sto_req_ops sto_read_req_ops = {
	.decode_cdb = sto_read_req_decode_cdb,
	.exec_constructor = sto_read_req_exec_constructor,
	.response = sto_read_req_response,
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

static int
sto_readlink_req_exec(struct sto_req *req)
{
	struct sto_readlink_req *readlink_req = STO_REQ_TYPE(req, readlink);
	struct sto_readlink_req_params *params = &readlink_req->params;
	struct sto_rpc_readlink_args args = {
		.priv = req,
		.done = sto_req_exec_done,
		.buf = &readlink_req->buf,
	};

	return sto_rpc_readlink(params->file, &args);
}

static int
sto_readlink_req_exec_constructor(struct sto_req *req, int state)
{
	switch (state) {
	case 0:
		return sto_req_add_exec(req, sto_readlink_req_exec, NULL);
	default:
		return 0;
	}
}

static void
sto_readlink_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
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

	sto_req_free(req);
	rte_free(readlink_req);
}

struct sto_req_ops sto_readlink_req_ops = {
	.decode_cdb = sto_readlink_req_decode_cdb,
	.exec_constructor = sto_readlink_req_exec_constructor,
	.response = sto_readlink_req_response,
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

static int
sto_readdir_req_exec(struct sto_req *req)
{
	struct sto_readdir_req *readdir_req = STO_REQ_TYPE(req, readdir);
	struct sto_readdir_req_params *params = &readdir_req->params;
	struct sto_rpc_readdir_args args = {
		.priv = req,
		.done = sto_req_exec_done,
		.dirents = &readdir_req->dirents,
	};

	return sto_rpc_readdir(params->dirpath, &args);
}

static int
sto_readdir_req_exec_constructor(struct sto_req *req, int state)
{
	switch (state) {
	case 0:
		return sto_req_add_exec(req, sto_readdir_req_exec, NULL);
	default:
		return 0;
	}
}

static void
sto_readdir_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
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

	sto_req_free(req);
	rte_free(readdir_req);
}

struct sto_req_ops sto_readdir_req_ops = {
	.decode_cdb = sto_readdir_req_decode_cdb,
	.exec_constructor = sto_readdir_req_exec_constructor,
	.response = sto_readdir_req_response,
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

static int
sto_tree_req_exec(struct sto_req *req)
{
	struct sto_tree_req *tree_req = STO_REQ_TYPE(req, tree);
	struct sto_tree_req_params *params = &tree_req->params;
	struct sto_tree_args args = {
		.priv = req,
		.done = sto_req_exec_done,
		.tree_root = &tree_req->tree_root,
	};

	return sto_tree(params->dirpath, params->depth, params->only_dirs, &args);
}

static int
sto_tree_req_exec_constructor(struct sto_req *req, int state)
{
	switch (state) {
	case 0:
		return sto_req_add_exec(req, sto_tree_req_exec, NULL);
	default:
		return 0;
	}
}

static void
sto_tree_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_tree_req *tree_req = STO_REQ_TYPE(req, tree);
	struct sto_tree_req_params *params = &tree_req->params;
	struct sto_tree_node *tree_root = &tree_req->tree_root;

	if (params->info_json) {
		params->info_json(tree_root, w);
		return;
	}

	sto_tree_info_json(tree_root, w);
}

static void
sto_tree_req_free(struct sto_req *req)
{
	struct sto_tree_req *tree_req = STO_REQ_TYPE(req, tree);

	sto_tree_req_params_free(&tree_req->params);
	sto_tree_free(&tree_req->tree_root);

	sto_req_free(req);
	rte_free(tree_req);
}

struct sto_req_ops sto_tree_req_ops = {
	.decode_cdb = sto_tree_req_decode_cdb,
	.exec_constructor = sto_tree_req_exec_constructor,
	.response = sto_tree_req_response,
	.free = sto_tree_req_free,
};

int
sto_lib_init(void)
{
	g_sto_exec_poller = SPDK_POLLER_REGISTER(sto_exec_poll, NULL, STO_EXEC_POLL_PERIOD);
	if (spdk_unlikely(!g_sto_exec_poller)) {
		SPDK_ERRLOG("Cann't register the STO req poller\n");
		return -ENOMEM;
	}

	g_sto_rollback_poller = SPDK_POLLER_REGISTER(sto_rollback_poll, NULL, STO_ROLLBACK_POLL_PERIOD);
	if (spdk_unlikely(!g_sto_rollback_poller)) {
		SPDK_ERRLOG("Cann't register the STO req poller\n");
		return -ENOMEM;
	}


	return 0;
}

void
sto_lib_fini(void)
{
	spdk_poller_unregister(&g_sto_exec_poller);
	spdk_poller_unregister(&g_sto_rollback_poller);
}
