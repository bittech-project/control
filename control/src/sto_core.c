#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_core.h"
#include "sto_utils.h"
#include "sto_component.h"
#include "sto_req.h"
#include "sto_err.h"

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

static void sto_core_process(struct sto_core_req *core_req);

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

		sto_core_process(core_req);
	}

	return SPDK_POLLER_BUSY;
}

static struct sto_core_req *
sto_core_req_alloc(const struct spdk_json_val *params)
{
	struct sto_core_req *core_req;

	core_req = rte_zmalloc(NULL, sizeof(*core_req), 0);
	if (spdk_unlikely(!core_req)) {
		SPDK_ERRLOG("Cann't allocate memory for core req\n");
		return NULL;
	}

	core_req->params = params;
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
sto_core_req_process(struct sto_core_req *core_req)
{
	TAILQ_INSERT_TAIL(&g_sto_core_req_list, core_req, list);
}

static void
sto_core_req_submit(struct sto_core_req *core_req)
{
	sto_core_req_process(core_req);
}

int
sto_core_process_json(const struct spdk_json_val *params, struct sto_core_args *args)
{
	struct sto_core_req *req;

	req = sto_core_req_alloc(params);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to create STO req\n");
		return -ENOMEM;
	}

	sto_core_req_init_cb(req, args->done, args->priv);

	sto_core_req_submit(req);

	return 0;
}

struct sto_core_component_ctx {
	void *buf;

	const struct spdk_json_val *values;
	size_t values_cnt;

	void *user_priv;
	sto_core_req_done_t user_done;
};

static int
sto_core_component_ctx_write_cb(void *cb_ctx, const void *data, size_t size)
{
	struct sto_core_component_ctx *ctx = cb_ctx;
	void *end;
	ssize_t rc;

	ctx->buf = calloc(1, size);
	if (spdk_unlikely(!ctx->buf)) {
		SPDK_ERRLOG("Failed to alloc buf: size=%zu\n", size);
		return -ENOMEM;
	}

	memcpy(ctx->buf, data, size);

	rc = spdk_json_parse(ctx->buf, size, NULL, 0, &end, 0);
	if (spdk_unlikely(rc < 0)) {
		SPDK_NOTICELOG("Parsing JSON failed (%zd)\n", rc);
		return rc;
	}

	ctx->values_cnt = rc;

	ctx->values = calloc(ctx->values_cnt, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!ctx->values)) {
		SPDK_ERRLOG("Failed to alloc json values: cnt=%zu\n",
			    ctx->values_cnt);
		return -ENOMEM;
	}

	rc = spdk_json_parse(ctx->buf, size, (struct spdk_json_val *) ctx->values,
			     ctx->values_cnt, &end, 0);
	if (rc != (ssize_t) ctx->values_cnt) {
		SPDK_ERRLOG("Parsing JSON failed (%zd)\n", rc);
		return -EINVAL;
	}

	return 0;
}

static void sto_core_component_ctx_free(struct sto_core_component_ctx *ctx);

static struct sto_core_component_ctx *
sto_core_component_ctx_alloc(const char *component, const char *object, const char *op_name,
			     void *params, sto_core_dump_params_t dump_params,
			     struct sto_core_args *args)
{
	struct sto_core_component_ctx *ctx;
	struct spdk_json_write_ctx *w;
	int rc;

	ctx = rte_zmalloc(NULL, sizeof(*ctx), 0);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc component context\n");
		return NULL;
	}

	w = spdk_json_write_begin(sto_core_component_ctx_write_cb, ctx, 0);
	if (spdk_unlikely(!w)) {
		SPDK_ERRLOG("Failed to alloc SPDK json write context\n");
		goto free_ctx;
	}

	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, component, object);
	spdk_json_write_named_string(w, "op", op_name);

	if (dump_params) {
		dump_params(params, w);
	}

	spdk_json_write_object_end(w);

	rc = spdk_json_write_end(w);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to write json context\n");
		goto free_ctx;
	}

	ctx->user_priv = args->priv;
	ctx->user_done = args->done;

	return ctx;

free_ctx:
	sto_core_component_ctx_free(ctx);

	return NULL;
}

static void
sto_core_component_ctx_free(struct sto_core_component_ctx *ctx)
{
	free((struct spdk_json_val *) ctx->values);
	free(ctx->buf);

	rte_free(ctx);
}

static void
sto_core_process_component_done(struct sto_core_req *core_req)
{
	struct sto_core_component_ctx *ctx = core_req->priv;

	core_req->priv = ctx->user_priv;
	ctx->user_done(core_req);

	sto_core_component_ctx_free(ctx);
}

int
sto_core_process_component(const char *component, const char *object, const char *op_name,
			   void *params, sto_core_dump_params_t dump_params,
			   struct sto_core_args *args)
{
	struct sto_core_component_ctx *ctx;
	struct sto_core_args __args = {};
	int rc = 0;

	ctx = sto_core_component_ctx_alloc(component, object, op_name, params, dump_params, args);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc component context\n");
		return -ENOMEM;
	}

	__args.priv = ctx;
	__args.done = sto_core_process_component_done;

	rc = sto_core_process_json(ctx->values, &__args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to core process json\n");
		goto free_ctx;
	}

	return 0;

free_ctx:
	sto_core_component_ctx_free(ctx);

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
sto_core_decode_ops(const struct sto_op_table *op_table,
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

	op = sto_op_table_find(op_table, op_name);
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

	rc = sto_req_type_parse_params(&req->type, op->decoder, iter, op->req_params_constructor);
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
	const struct sto_op_table *op_table;
	const struct sto_ops *op;
	struct sto_req_context *req_ctx;
	int rc = 0;

	if (spdk_unlikely(!core_req->params)) {
		return -EINVAL;
	}

	sto_json_iter_init(&iter, core_req->params);

	op_table = sto_core_component_decode(&iter);
	if (IS_ERR_OR_NULL(op_table)) {
		SPDK_ERRLOG("Failed to decode component: req[%p]\n", core_req);
		return IS_ERR(op) ? PTR_ERR(op) : -ENOENT;
	}

	rc = sto_json_iter_next(&iter);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to get next JSON object\n");
		return rc;
	}

	op = sto_core_decode_ops(op_table, &iter);
	if (IS_ERR(op)) {
		SPDK_ERRLOG("Failed to decode ops: req[%p]\n", core_req);
		return PTR_ERR(op);
	}

	rc = sto_json_iter_next(&iter);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to get next JSON object\n");
		return rc;
	}

	req_ctx = sto_core_parse_ops(op, &iter);
	if (spdk_unlikely(!req_ctx)) {
		SPDK_ERRLOG("Failed to parse ops: req[%p]\n", core_req);
		return -EINVAL;
	}

	sto_core_req_init_req_ctx(core_req, req_ctx);

	sto_core_req_set_state(core_req, STO_CORE_REQ_STATE_EXEC);
	sto_core_req_process(core_req);

	return rc;
}

static void
sto_core_req_exec_done(void *priv)
{
	struct sto_core_req *core_req = priv;

	sto_core_req_set_state(core_req, STO_CORE_REQ_STATE_DONE);
	sto_core_req_process(core_req);
}

static void
sto_core_req_exec(struct sto_core_req *core_req)
{
	struct sto_req *req = STO_REQ(core_req->req_ctx);

	sto_req_step_start(req);
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

	req = STO_REQ(core_req->req_ctx);
	req->type.response(req, w);

	return;
}

void
sto_core_req_free(struct sto_core_req *core_req)
{
	if (core_req->req_ctx) {
		struct sto_req *req = STO_REQ(core_req->req_ctx);

		sto_req_free(req);
		core_req->req_ctx = NULL;
	}

	rte_free(core_req);
}

static void
sto_core_process(struct sto_core_req *core_req)
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

int
sto_core_init(void)
{
	int rc;

	g_sto_core_req_poller = SPDK_POLLER_REGISTER(sto_core_req_poll, NULL, STO_CORE_REQ_POLL_PERIOD);
	if (spdk_unlikely(!g_sto_core_req_poller)) {
		SPDK_ERRLOG("Cann't register the STO core req poller\n");
		return -ENOMEM;
	}

	rc = sto_req_lib_init();
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("sto_req_lib_init() failed, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

void
sto_core_fini(void)
{
	sto_req_lib_fini();
	spdk_poller_unregister(&g_sto_core_req_poller);
}
