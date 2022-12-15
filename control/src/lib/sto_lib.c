#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "sto_rpc_aio.h"
#include "sto_err.h"

#define STO_EXEC_POLL_PERIOD		1000 /* 1ms */
#define STO_ROLLBACK_POLL_PERIOD	1000 /* 1ms */

static struct spdk_poller *g_sto_exec_poller;
static struct spdk_poller *g_sto_rollback_poller;

TAILQ_HEAD(sto_req_list, sto_req);

static struct sto_req_list g_sto_req_exec_list = TAILQ_HEAD_INITIALIZER(g_sto_req_exec_list);
static struct sto_req_list g_sto_req_rollback_list = TAILQ_HEAD_INITIALIZER(g_sto_req_rollback_list);


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

static void sto_ops_decoder_params_free(const struct sto_ops_decoder *decoder, void *ops_params);

static void *
sto_ops_decoder_params_parse(const struct sto_ops_decoder *decoder, const struct spdk_json_val *values)
{
	void *ops_params;
	uint32_t params_size;

	if (!values) {
		return decoder->allow_empty ? NULL : ERR_PTR(-EINVAL);
	}

	params_size = decoder->params_size;

	ops_params = rte_zmalloc(NULL, params_size, 0);
	if (spdk_unlikely(!ops_params)) {
		SPDK_ERRLOG("Failed to alloc ops decoder params\n");
		return ERR_PTR(-ENOMEM);
	}

	if (spdk_json_decode_object(values, decoder->decoders, decoder->num_decoders, ops_params)) {
		SPDK_ERRLOG("Failed to decode ops_params\n");
		sto_ops_decoder_params_free(decoder, ops_params);
		return ERR_PTR(-EINVAL);
	}

	return ops_params;
}

static void
sto_ops_decoder_params_free(const struct sto_ops_decoder *decoder, void *ops_params)
{
	if (!ops_params) {
		return;
	}

	if (decoder->params_deinit) {
		decoder->params_deinit(ops_params);
	}

	rte_free(ops_params);
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

static int
sto_req_type_init(struct sto_req_type *type, const struct sto_req_properties *properties)
{
	if (properties->params_size) {
		type->params = rte_zmalloc(NULL, properties->params_size, 0);
		if (spdk_unlikely(!type->params)) {
			SPDK_ERRLOG("Failed to alloc req type params\n");
			return -ENOMEM;
		}

		type->params_deinit = properties->params_deinit;
	}

	if (properties->priv_size) {
		type->priv = rte_zmalloc(NULL, properties->priv_size, 0);
		if (spdk_unlikely(!type->priv)) {
			SPDK_ERRLOG("Failed to alloc req type priv\n");
			rte_free(type->params);
			return -ENOMEM;
		}

		type->priv_deinit = properties->priv_deinit;
	}

	type->response = properties->response;
	type->exec_constructor = properties->exec_constructor;

	return 0;
}

int
sto_req_type_parse_params(struct sto_req_type *type, const struct sto_ops_decoder *decoder,
			  const struct spdk_json_val *values,
			  sto_ops_req_params_constructor_t req_params_constructor)
{
	void *ops_params = NULL;
	int rc = 0;

	assert(!decoder || req_params_constructor);

	if (!req_params_constructor) {
		return 0;
	}

	if (decoder) {
		ops_params = sto_ops_decoder_params_parse(decoder, values);
		if (IS_ERR(ops_params)) {
			SPDK_ERRLOG("Failed to parse ops params\n");
			return PTR_ERR(ops_params);
		}
	}

	rc = req_params_constructor(type->params, ops_params);

	if (decoder) {
		sto_ops_decoder_params_free(decoder, ops_params);
	}

	return rc;
}

static void
sto_req_type_deinit(struct sto_req_type *type)
{
	if (type->params) {
		if (type->params_deinit) {
			type->params_deinit(type->params);
		}

		rte_free(type->params);
	}

	if (type->priv) {
		if (type->priv_deinit) {
			type->priv_deinit(type->priv);
		}

		rte_free(type->priv);
	}
}

struct sto_req *
sto_req_alloc(const struct sto_req_properties *properties)
{
	struct sto_req *req;
	int rc;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc STO req\n");
		return NULL;
	}

	rc = sto_req_type_init(&req->type, properties);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init STO req\n");
		rte_free(req);
		return NULL;
	}

	TAILQ_INIT(&req->exe_queue);
	TAILQ_INIT(&req->rollback_stack);

	return req;
}

void
sto_req_free(struct sto_req *req)
{
	sto_req_type_deinit(&req->type);

	sto_exec_list_free(&req->exe_queue);
	sto_exec_list_free(&req->rollback_stack);

	rte_free(req);
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

		rc = req->type.exec_constructor(req, req->state);
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

static void
sto_write_req_params_deinit(void *params_ptr)
{
	struct sto_write_req_params *params = params_ptr;

	free((char *) params->file);
	free(params->data);
}

static int
sto_write_req_exec(struct sto_req *req)
{
	struct sto_write_req_params *params = req->type.params;
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

const struct sto_req_properties sto_write_req_properties = {
	.params_size = sizeof(struct sto_write_req_params),
	.params_deinit = sto_write_req_params_deinit,

	.response = sto_dummy_req_response,
	.exec_constructor = sto_write_req_exec_constructor,
};

struct sto_read_req_priv {
	char *buf;
};

static void
sto_read_req_priv_deinit(void *priv_ptr)
{
	struct sto_read_req_priv *priv = priv_ptr;
	free(priv->buf);
}

static void
sto_read_req_params_deinit(void *params_ptr)
{
	struct sto_read_req_params *params = params_ptr;
	free((char *) params->file);
}

static int
sto_read_req_exec(struct sto_req *req)
{
	struct sto_read_req_priv *priv = req->type.priv;
	struct sto_read_req_params *params = req->type.params;
	struct sto_rpc_readfile_args args = {
		.priv = req,
		.done = sto_req_exec_done,
		.buf = &priv->buf,
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
	struct sto_read_req_priv *priv = req->type.priv;

	spdk_json_write_string(w, priv->buf);
}

const struct sto_req_properties sto_read_req_properties = {
	.params_size = sizeof(struct sto_read_req_params),
	.params_deinit = sto_read_req_params_deinit,

	.priv_size = sizeof(struct sto_read_req_priv),
	.priv_deinit = sto_read_req_priv_deinit,

	.response = sto_read_req_response,
	.exec_constructor = sto_read_req_exec_constructor,
};

static void
sto_readlink_req_params_deinit(void *params_ptr)
{
	struct sto_readlink_req_params *params = params_ptr;
	free((char *) params->file);
}

struct sto_readlink_req_priv {
	char *buf;
};

static void
sto_readlink_req_priv_deinit(void *priv_ptr)
{
	struct sto_readlink_req_priv *priv = priv_ptr;
	free(priv->buf);
}

static int
sto_readlink_req_exec(struct sto_req *req)
{
	struct sto_readlink_req_priv *priv = req->type.priv;
	struct sto_readlink_req_params *params = req->type.params;
	struct sto_rpc_readlink_args args = {
		.priv = req,
		.done = sto_req_exec_done,
		.buf = &priv->buf,
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
	struct sto_readlink_req_priv *priv = req->type.priv;

	spdk_json_write_string(w, priv->buf);
}

const struct sto_req_properties sto_readlink_req_properties = {
	.params_size = sizeof(struct sto_readlink_req_params),
	.params_deinit = sto_readlink_req_params_deinit,

	.priv_size = sizeof(struct sto_readlink_req_priv),
	.priv_deinit = sto_readlink_req_priv_deinit,

	.response = sto_readlink_req_response,
	.exec_constructor = sto_readlink_req_exec_constructor,
};

static void
sto_readdir_req_params_deinit(void *params_ptr)
{
	struct sto_readdir_req_params *params = params_ptr;
	free((char *) params->name);
	free(params->dirpath);
}

struct sto_readdir_req_priv {
	struct sto_dirents dirents;
};

static void
sto_readdir_req_priv_deinit(void *priv_ptr)
{
	struct sto_readdir_req_priv *priv = priv_ptr;
	sto_dirents_free(&priv->dirents);
}

static int
sto_readdir_req_exec(struct sto_req *req)
{
	struct sto_readdir_req_priv *priv = req->type.priv;
	struct sto_readdir_req_params *params = req->type.params;
	struct sto_rpc_readdir_args args = {
		.priv = req,
		.done = sto_req_exec_done,
		.dirents = &priv->dirents,
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
	struct sto_readdir_req_priv *priv = req->type.priv;
	struct sto_readdir_req_params *params = req->type.params;
	struct sto_dirents_json_cfg cfg = {
		.name = params->name,
		.exclude_list = params->exclude_list,
	};

	sto_dirents_info_json(&priv->dirents, &cfg, w);
}

const struct sto_req_properties sto_readdir_req_properties = {
	.params_size = sizeof(struct sto_readdir_req_params),
	.params_deinit = sto_readdir_req_params_deinit,

	.priv_size = sizeof(struct sto_readdir_req_priv),
	.priv_deinit = sto_readdir_req_priv_deinit,

	.response = sto_readdir_req_response,
	.exec_constructor = sto_readdir_req_exec_constructor,
};

static void
sto_tree_req_params_deinit(void *params_ptr)
{
	struct sto_tree_req_params *params = params_ptr;
	free(params->dirpath);
}

struct sto_tree_req_priv {
	struct sto_tree_node tree_root;
};

static void
sto_tree_req_priv_deinit(void *priv_ptr)
{
	struct sto_tree_req_priv *priv = priv_ptr;
	sto_tree_free(&priv->tree_root);
}

static int
sto_tree_req_exec(struct sto_req *req)
{
	struct sto_tree_req_priv *priv = req->type.priv;
	struct sto_tree_req_params *params = req->type.params;
	struct sto_tree_args args = {
		.priv = req,
		.done = sto_req_exec_done,
		.tree_root = &priv->tree_root,
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
	struct sto_tree_req_priv *priv = req->type.priv;
	struct sto_tree_req_params *params = req->type.params;
	struct sto_tree_node *tree_root = &priv->tree_root;

	if (params->info_json) {
		params->info_json(tree_root, w);
		return;
	}

	sto_tree_info_json(tree_root, w);
}

const struct sto_req_properties sto_tree_req_properties = {
	.params_size = sizeof(struct sto_tree_req_params),
	.params_deinit = sto_tree_req_params_deinit,

	.priv_size = sizeof(struct sto_tree_req_priv),
	.priv_deinit = sto_tree_req_priv_deinit,

	.response = sto_tree_req_response,
	.exec_constructor = sto_tree_req_exec_constructor,
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
