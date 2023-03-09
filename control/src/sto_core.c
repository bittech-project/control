#include "sto_core.h"

#include <spdk/stdinc.h>
#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/queue.h>

#include "sto_json.h"
#include "sto_component.h"
#include "sto_req.h"
#include "sto_err.h"
#include "sto_lib.h"

struct spdk_json_write_ctx;
struct sto_shash;

#define STO_CORE_REQ_POLL_PERIOD	1000 /* 1ms */

static struct spdk_poller *g_sto_core_req_poller;

static TAILQ_HEAD(sto_core_req_list, sto_core_req) g_sto_core_req_list =
	TAILQ_HEAD_INITIALIZER(g_sto_core_req_list);


static const char *const sto_core_req_state_names[] = {
	[STO_CORE_REQ_STATE_PARSE]	= "STATE_PARSE",
	[STO_CORE_REQ_STATE_EXEC]	= "STATE_EXEC",
	[STO_CORE_REQ_STATE_DONE]	= "STATE_DONE",
};

const char *
sto_core_req_state_name(enum sto_core_req_state state)
{
	size_t index = state;

	if (spdk_unlikely(index >= SPDK_COUNTOF(sto_core_req_state_names))) {
		assert(0);
	}

	return sto_core_req_state_names[index];
}

static void sto_core_process_req(struct sto_core_req *core_req);

static int
sto_core_req_poll(void *ctx)
{
	struct sto_core_req_list core_req_list = TAILQ_HEAD_INITIALIZER(core_req_list);
	struct sto_core_req *core_req, *tmp;

	if (TAILQ_EMPTY(&g_sto_core_req_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&core_req_list, &g_sto_core_req_list, sto_core_req, list);

	TAILQ_FOREACH_SAFE(core_req, &core_req_list, list, tmp) {
		TAILQ_REMOVE(&core_req_list, core_req, list);

		sto_core_process_req(core_req);
	}

	return SPDK_POLLER_BUSY;
}

static struct sto_core_req *
sto_core_req_alloc(const struct spdk_json_val *params, bool internal)
{
	struct sto_core_req *core_req;

	core_req = calloc(1, sizeof(*core_req));
	if (spdk_unlikely(!core_req)) {
		SPDK_ERRLOG("Cann't allocate memory for core req\n");
		return NULL;
	}

	core_req->params = params;
	core_req->internal = internal;
	sto_core_req_set_state(core_req, STO_CORE_REQ_STATE_PARSE);

	return core_req;
}

static void
sto_core_req_init_cb(struct sto_core_req *core_req, sto_core_req_done_t done, void *priv)
{
	core_req->done = done;
	core_req->priv = priv;
}

static void
sto_core_queue_req(struct sto_core_req *core_req)
{
	TAILQ_INSERT_TAIL(&g_sto_core_req_list, core_req, list);
}

static void
sto_core_submit_req(struct sto_core_req *core_req)
{
	sto_core_queue_req(core_req);
}

static int
core_process(const struct spdk_json_val *params, sto_core_req_done_t done,
	     void *priv, bool internal)
{
	struct sto_core_req *req;

	req = sto_core_req_alloc(params, internal);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to create STO req\n");
		return -ENOMEM;
	}

	sto_core_req_init_cb(req, done, priv);

	sto_core_submit_req(req);

	return 0;
}

int
sto_core_process(const struct spdk_json_val *params, sto_core_req_done_t done, void *priv)
{
	return core_process(params, done, priv, false);
}

struct sto_core_ctx {
	struct sto_json_ctx json_ctx;

	sto_core_req_done_t user_done;
	void *user_priv;
};

static void sto_core_ctx_free(struct sto_core_ctx *ctx);

static int
core_ctx_write_head_cb(void *cb_ctx, struct spdk_json_write_ctx *w)
{
	const struct sto_json_head_raw *head = cb_ctx;
	int rc = 0;

	spdk_json_write_object_begin(w);

	rc = sto_json_head_raw_dump(head, w);

	spdk_json_write_object_end(w);

	return rc;
}

static struct sto_core_ctx *
sto_core_ctx_alloc(const struct sto_json_head_raw *head,
		   sto_core_req_done_t done, void *priv)
{
	struct sto_core_ctx *ctx;
	int rc;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc component context\n");
		return NULL;
	}

	rc = sto_json_ctx_write(&ctx->json_ctx, false, core_ctx_write_head_cb, (void *) head);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to dump STO JSON head\n");
		goto free_ctx;
	}

	ctx->user_done = done;
	ctx->user_priv = priv;

	return ctx;

free_ctx:
	free(ctx);

	return NULL;
}

static void
sto_core_ctx_free(struct sto_core_ctx *ctx)
{
	sto_json_ctx_destroy(&ctx->json_ctx);
	free(ctx);
}

static void
sto_core_process_raw_done(struct sto_core_req *core_req)
{
	struct sto_core_ctx *ctx = core_req->priv;

	core_req->priv = ctx->user_priv;
	ctx->user_done(core_req);

	sto_core_ctx_free(ctx);
}

int
sto_core_process_raw(const struct sto_json_head_raw *head,
		     sto_core_req_done_t done, void *priv)
{
	struct sto_core_ctx *ctx;
	int rc = 0;

	ctx = sto_core_ctx_alloc(head, done, priv);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc component context\n");
		return -ENOMEM;
	}

	rc = core_process(ctx->json_ctx.values, sto_core_process_raw_done, ctx, true);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to core process json\n");
		goto free_ctx;
	}

	return 0;

free_ctx:
	sto_core_ctx_free(ctx);

	return rc;
}

static void sto_core_req_exec_done(void *priv);

static void
sto_core_req_init_req_ctx(struct sto_core_req *core_req, struct sto_req_context *req_ctx)
{
	req_ctx->priv = core_req;
	req_ctx->done = sto_core_req_exec_done;
	req_ctx->err_ctx = &core_req->err_ctx;

	core_req->req_ctx = req_ctx;
}

static const struct sto_ops *
sto_core_decode_ops(const struct sto_shash *ops_map,
		    const struct sto_json_iter *iter)
{
	const struct sto_ops *op;
	char *op_name = NULL;
	int rc = 0;

	rc = sto_json_iter_decode_str(iter, "op", &op_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode op, rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	op = sto_ops_map_find(ops_map, op_name);
	if (!op) {
		SPDK_ERRLOG("Failed to find op %s\n", op_name);
		free(op_name);
		return ERR_PTR(-EINVAL);
	}

	free(op_name);

	return op;
}

static struct sto_req_context *
sto_core_parse_ops(const struct sto_ops *op, const struct sto_json_iter *iter)
{
	struct sto_req *req = NULL;
	int rc;

	req = sto_req_alloc(op->req_properties);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to construct req\n");
		return NULL;
	}

	rc = sto_req_type_parse_params(&req->type, op->params_properties, iter, op->req_params_constructor);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode CDB for req[%p], rc=%d\n", req, rc);
		sto_req_free(req);
		return NULL;
	}

	return &req->ctx;
}

static int
sto_core_req_parse(struct sto_core_req *core_req)
{
	struct sto_json_iter iter;
	const struct sto_shash *ops_map;
	const struct sto_ops *op;
	struct sto_req_context *req_ctx;
	int rc = 0;

	if (spdk_unlikely(!core_req->params)) {
		return -EINVAL;
	}

	sto_json_iter_init(&iter, core_req->params);

	ops_map = sto_core_component_decode(&iter, core_req->internal);
	if (IS_ERR_OR_NULL(ops_map)) {
		SPDK_ERRLOG("Failed to decode component: req[%p]\n", core_req);
		return IS_ERR(op) ? (int) PTR_ERR(op) : -ENOENT;
	}

	if (!sto_json_iter_next(&iter)) {
		SPDK_ERRLOG("Failed to get next JSON object\n");
		return -EINVAL;
	}

	op = sto_core_decode_ops(ops_map, &iter);
	if (IS_ERR(op)) {
		SPDK_ERRLOG("Failed to decode ops: req[%p]\n", core_req);
		return PTR_ERR(op);
	}

	if (!sto_json_iter_next(&iter)) {
		SPDK_ERRLOG("Failed to get next JSON object\n");
		return -EINVAL;
	}

	req_ctx = sto_core_parse_ops(op, &iter);
	if (spdk_unlikely(!req_ctx)) {
		SPDK_ERRLOG("Failed to parse ops: req[%p]\n", core_req);
		return -EINVAL;
	}

	sto_core_req_init_req_ctx(core_req, req_ctx);

	sto_core_req_set_state(core_req, STO_CORE_REQ_STATE_EXEC);
	sto_core_queue_req(core_req);

	return rc;
}

static void
sto_core_req_exec_done(void *priv)
{
	struct sto_core_req *core_req = priv;

	sto_core_req_set_state(core_req, STO_CORE_REQ_STATE_DONE);
	sto_core_queue_req(core_req);
}

static void
sto_core_req_exec(struct sto_core_req *core_req)
{
	struct sto_req *req = sto_req_from_ctx(core_req->req_ctx);

	sto_req_run(req);
}

static void
sto_core_req_done(struct sto_core_req *core_req)
{
	core_req->done(core_req);
}

void
sto_core_req_response(struct sto_core_req *core_req, struct spdk_json_write_ctx *w)
{
	struct sto_req *req;
	struct sto_err_context *err = &core_req->err_ctx;

	SPDK_ERRLOG("req[%p] end response: rc=%d\n", core_req, err->rc);

	if (err->rc) {
		sto_status_failed(w, err);
		return;
	}

	req = sto_req_from_ctx(core_req->req_ctx);
	req->type.response(req, w);

	return;
}

void
sto_core_req_free(struct sto_core_req *core_req)
{
	if (core_req->req_ctx) {
		struct sto_req *req = sto_req_from_ctx(core_req->req_ctx);

		sto_req_free(req);
		core_req->req_ctx = NULL;
	}

	free(core_req);
}

static void
sto_core_process_req(struct sto_core_req *core_req)
{
	int rc = 0;

	switch (core_req->state) {
	case STO_CORE_REQ_STATE_PARSE:
		rc = sto_core_req_parse(core_req);
		break;
	case STO_CORE_REQ_STATE_EXEC:
		sto_core_req_exec(core_req);
		break;
	case STO_CORE_REQ_STATE_DONE:
		sto_core_req_done(core_req);
		break;
	default:
		SPDK_ERRLOG("core req (%p) in state %s, but shouldn't be\n",
			    core_req, sto_core_req_state_name(core_req->state));
		assert(0);
	}

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("core req (%p) in state %s failed, rc=%d\n",
			    core_req, sto_core_req_state_name(core_req->state), rc);
		sto_err(&core_req->err_ctx, rc);
		sto_core_req_done(core_req);
	}

	return;
}

void
sto_core_init(sto_core_init_fn cb_fn, void *cb_arg)
{
	int rc;

	g_sto_core_req_poller = SPDK_POLLER_REGISTER(sto_core_req_poll, NULL, STO_CORE_REQ_POLL_PERIOD);
	if (spdk_unlikely(!g_sto_core_req_poller)) {
		SPDK_ERRLOG("Cann't register the STO core req poller\n");
		rc = -ENOMEM;
		goto out_err;
	}

	rc = sto_req_lib_init();
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("sto_req_lib_init() failed, rc=%d\n", rc);
		goto out_err;
	}

	sto_core_component_init(cb_fn, cb_arg);

	return;

out_err:
	cb_fn(cb_arg, rc);
}

void
sto_core_fini(sto_core_fini_fn cb_fn, void *cb_arg)
{
	sto_req_lib_fini();
	spdk_poller_unregister(&g_sto_core_req_poller);

	sto_core_component_fini(cb_fn, cb_arg);
}
