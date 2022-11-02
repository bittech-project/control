#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_core.h"
#include "sto_subsystem.h"
#include "err.h"

#define STO_CORE_POLL_PERIOD	1000 /* 1ms */

struct spdk_poller *g_sto_core_poller;

static TAILQ_HEAD(sto_core_req_list, sto_core_req) g_sto_core_req_list
	= TAILQ_HEAD_INITIALIZER(g_sto_core_req_list);


static void sto_process_req(struct sto_core_req *req);

static const char *const sto_core_req_state_names[] = {
	[STO_CORE_REQ_STATE_PARSE]	= "STATE_PARSE",
	[STO_CORE_REQ_STATE_EXEC]	= "STATE_EXEC",
	[STO_CORE_REQ_STATE_RESPONSE]	= "STATE_RESPONSE",
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

static int
sto_core_poll(void *ctx)
{
	struct sto_core_req_list req_list = TAILQ_HEAD_INITIALIZER(req_list);
	struct sto_core_req *req, *tmp;

	if (TAILQ_EMPTY(&g_sto_core_req_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&req_list, &g_sto_core_req_list, sto_core_req, list);

	TAILQ_FOREACH_SAFE(req, &req_list, list, tmp) {
		TAILQ_REMOVE(&req_list, req, list);

		sto_process_req(req);
	}

	return SPDK_POLLER_BUSY;
}

struct sto_core_req *
sto_core_req_alloc(const struct spdk_json_val *params)
{
	struct sto_core_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for req\n");
		return NULL;
	}

	req->params = params;
	sto_core_req_set_state(req, STO_CORE_REQ_STATE_PARSE);

	return req;
}

void
sto_core_req_init_cb(struct sto_core_req *req, sto_core_req_response_t response, void *priv)
{
	req->response = response;
	req->priv = priv;
}

void
sto_core_req_free(struct sto_core_req *req)
{
	rte_free(req);
}

void
sto_core_req_process(struct sto_core_req *req)
{
	TAILQ_INSERT_TAIL(&g_sto_core_req_list, req, list);
}

void
sto_core_req_submit(struct sto_core_req *req)
{
	sto_core_req_process(req);
}

static struct sto_subsystem *
sto_core_req_get_subsystem(struct sto_core_req *req)
{
	struct sto_subsystem *subsystem;
	char *subsystem_name = NULL;
	int rc = 0;

	rc = sto_decode_object_str(req->params, "subsystem", &subsystem_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode subystem for req[%p], rc=%d\n", req, rc);
		return NULL;
	}

	subsystem = sto_subsystem_find(subsystem_name);
	if (spdk_unlikely(!subsystem)) {
		SPDK_ERRLOG("Failed to find %s subsystem\n", subsystem_name);
		goto out;
	}

out:
	free(subsystem_name);

	return subsystem;
}

static void sto_exec_done(void *priv);

static void
sto_core_req_init_ctx(struct sto_core_req *req, struct sto_context *ctx)
{
	ctx->priv = req;
	ctx->response = sto_exec_done;
	ctx->err_ctx = &req->err_ctx;

	req->ctx = ctx;
}

static int
sto_core_req_parse(struct sto_core_req *core_req)
{
	struct sto_subsystem *subsystem;
	const struct spdk_json_val *cdb;
	struct sto_context *ctx;
	int rc = 0;

	subsystem = sto_core_req_get_subsystem(core_req);
	if (spdk_unlikely(!subsystem)) {
		SPDK_ERRLOG("Failed to get subsystem for req[%p], rc=%d\n",
			    core_req, rc);
		return rc;
	}

	cdb = sto_decode_next_cdb(core_req->params);
	if (IS_ERR_OR_NULL(cdb)) {
		SPDK_ERRLOG("Failed to decode CDB for req[%p]\n", core_req);
		rc = PTR_ERR_OR_ZERO(cdb);
		return rc ?: -EINVAL;
	}

	ctx = sto_subsystem_parse(subsystem, cdb);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to %s to parse req\n", subsystem->name);
		rc = -EINVAL;
		goto out;
	}

	sto_core_req_init_ctx(core_req, ctx);

	sto_core_req_set_state(core_req, STO_CORE_REQ_STATE_EXEC);
	sto_core_req_process(core_req);

out:
	free((struct spdk_json_val *) cdb);

	return rc;
}

static void
sto_exec_done(void *priv)
{
	struct sto_core_req *core_req = priv;

	sto_core_req_set_state(core_req, STO_CORE_REQ_STATE_RESPONSE);
	sto_core_req_process(core_req);
}

static int
sto_core_req_exec(struct sto_core_req *core_req)
{
	struct sto_req *req = to_sto_req(core_req->ctx);
	struct sto_req_ops *ops = req->ops;

	return ops->exec(req);
}

static void
sto_core_req_response(struct sto_core_req *core_req)
{
	core_req->response(core_req);
}

void
sto_core_req_end_response(struct sto_core_req *core_req, struct spdk_json_write_ctx *w)
{
	struct sto_req *req;
	struct sto_req_ops *ops;
	struct sto_err_context *err = &core_req->err_ctx;

	SPDK_ERRLOG("req[%p] end response: rc=%d\n", core_req, err->rc);

	if (err->rc) {
		sto_status_failed(w, err);

		if (core_req->ctx) {
			req = to_sto_req(core_req->ctx);
			ops = req->ops;

			ops->free(req);
			core_req->ctx = NULL;
		}

		return;
	}

	req = to_sto_req(core_req->ctx);
	ops = req->ops;

	ops->end_response(req, w);
	ops->free(req);

	return;
}

static void
sto_process_req(struct sto_core_req *core_req)
{
	int rc = 0;

	switch (core_req->state) {
	case STO_CORE_REQ_STATE_PARSE:
		rc = sto_core_req_parse(core_req);
		break;
	case STO_CORE_REQ_STATE_EXEC:
		rc = sto_core_req_exec(core_req);
		break;
	case STO_CORE_REQ_STATE_RESPONSE:
		sto_core_req_response(core_req);
		break;
	default:
		SPDK_ERRLOG("req (%p) in state %s, but shouldn't be\n",
			    core_req, sto_core_req_state_name(core_req->state));
		assert(0);
	}

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("req (%p) in state %s failed, rc=%d\n",
			    core_req, sto_core_req_state_name(core_req->state), rc);
		sto_err(&core_req->err_ctx, rc);
		sto_core_req_response(core_req);
	}

	return;
}

int
sto_core_init(void)
{
	g_sto_core_poller = SPDK_POLLER_REGISTER(sto_core_poll, NULL, STO_CORE_POLL_PERIOD);
	if (spdk_unlikely(!g_sto_core_poller)) {
		SPDK_ERRLOG("Cann't register the STO req poller\n");
		return -ENOMEM;
	}

	return 0;
}

void
sto_core_fini(void)
{
	spdk_poller_unregister(&g_sto_core_poller);
}
