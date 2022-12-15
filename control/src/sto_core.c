#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_core.h"
#include "sto_lib.h"
#include "sto_subsystem.h"
#include "err.h"

#define STO_CORE_REQ_POLL_PERIOD	1000 /* 1ms */

static struct spdk_poller *g_sto_core_req_poller;

static TAILQ_HEAD(sto_component_list, sto_core_component) g_sto_components =
	TAILQ_HEAD_INITIALIZER(g_sto_components);

static TAILQ_HEAD(sto_core_req_list, sto_core_req) g_sto_core_req_list =
	TAILQ_HEAD_INITIALIZER(g_sto_core_req_list);


static struct sto_core_component *
_core_component_find(struct sto_component_list *list, const char *name)
{
	struct sto_core_component *component;

	TAILQ_FOREACH(component, list, list) {
		if (!strcmp(name, component->name)) {
			return component;
		}
	}

	return NULL;
}

static struct sto_core_component *
_core_component_next(struct sto_core_component *component, struct sto_component_list *list)
{
	return !component ? TAILQ_FIRST(list) : TAILQ_NEXT(component, list);
}

struct sto_core_component *
sto_core_component_find(const char *name)
{
	return _core_component_find(&g_sto_components, name);
}

struct sto_core_component *
sto_core_component_next(struct sto_core_component *component)
{
	return _core_component_next(component, &g_sto_components);
}

void
sto_core_add_component(struct sto_core_component *component)
{
	TAILQ_INSERT_TAIL(&g_sto_components, component, list);
}


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
sto_core_process_start(const struct spdk_json_val *params, struct sto_core_args *args)
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

static void sto_core_req_exec_done(void *priv);

static void
sto_core_req_init_req_ctx(struct sto_core_req *core_req, struct sto_req_context *req_ctx)
{
	req_ctx->priv = core_req;
	req_ctx->done = sto_core_req_exec_done;
	req_ctx->err_ctx = &core_req->err_ctx;

	core_req->req_ctx = req_ctx;
}

static struct sto_core_component *
sto_core_component_decode(const struct spdk_json_val *params)
{
	struct sto_core_component *component;
	char *component_name = NULL;
	int rc = 0;

	rc = sto_json_decode_object_name(params, &component_name);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode component name, rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	component = sto_core_component_find(component_name);

	free(component_name);

	if (spdk_unlikely(!component)) {
		SPDK_ERRLOG("Failed to find component\n");
		return ERR_PTR(-EINVAL);
	}

	return component;
}

static struct sto_req_context *
sto_core_parse_ops(const struct sto_ops *op, const struct spdk_json_val *params)
{
	struct sto_req *req = NULL;
	int rc;

	req = sto_req_alloc(op->req_properties);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to construct req\n");
		return NULL;
	}

	rc = sto_req_parse_params(req, op->decoder, params, op->req_params_constructor);
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
	struct sto_core_component *component;
	const struct sto_ops *op;
	const struct spdk_json_val *params_cdb = NULL;
	struct sto_req_context *req_ctx;
	int rc = 0;

	component = sto_core_component_decode(core_req->params);
	if (IS_ERR(component)) {
		SPDK_ERRLOG("Failed to parse component for req[%p]\n", core_req);
		return PTR_ERR(component);
	}

	op = component->decode_ops(core_req->params, &params_cdb);
	if (IS_ERR(op)) {
		SPDK_ERRLOG("%s component failed to decode ops\n", component->name);
		return PTR_ERR(op);
	}

	req_ctx = sto_core_parse_ops(op, params_cdb);
	if (spdk_unlikely(!req_ctx)) {
		SPDK_ERRLOG("Failed to parse ops\n");
		rc = -EINVAL;
		goto out;
	}

	sto_core_req_init_req_ctx(core_req, req_ctx);

	sto_core_req_set_state(core_req, STO_CORE_REQ_STATE_EXEC);
	sto_core_req_process(core_req);

out:
	free((struct spdk_json_val *) params_cdb);

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

	sto_req_process_start(req);
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
	req->ops->response(req, w);

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

	rc = sto_lib_init();
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("sto_lib_init() failed, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

void
sto_core_fini(void)
{
	sto_lib_fini();
	spdk_poller_unregister(&g_sto_core_req_poller);
}
