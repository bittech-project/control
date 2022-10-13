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

#define STO_REQ_POLL_PERIOD	1000 /* 1ms */

struct spdk_poller *g_sto_req_poller;

static TAILQ_HEAD(sto_req_list, sto_req) g_sto_req_list
	= TAILQ_HEAD_INITIALIZER(g_sto_req_list);


static void sto_process_req(struct sto_req *req);

static const char *const sto_req_state_names[] = {
	[STO_REQ_STATE_PARSE]		= "STATE_PARSE",
	[STO_REQ_STATE_EXEC]		= "STATE_EXEC",
	[STO_REQ_STATE_RESPONSE]	= "STATE_RESPONSE",
};

const char *
sto_req_state_name(enum sto_req_state state)
{
	size_t index = state;

	if (spdk_unlikely(index >= SPDK_COUNTOF(sto_req_state_names))) {
		assert(0);
	}

	return sto_req_state_names[index];
}

static int
sto_req_poll(void *ctx)
{
	struct sto_req_list req_list = TAILQ_HEAD_INITIALIZER(req_list);
	struct sto_req *req, *tmp;

	if (TAILQ_EMPTY(&g_sto_req_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&req_list, &g_sto_req_list, sto_req, list);

	TAILQ_FOREACH_SAFE(req, &req_list, list, tmp) {
		TAILQ_REMOVE(&req_list, req, list);

		sto_process_req(req);
	}

	return SPDK_POLLER_BUSY;
}

struct sto_req *
sto_req_alloc(const struct spdk_json_val *params)
{
	struct sto_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for req\n");
		return NULL;
	}

	req->params = params;
	sto_req_set_state(req, STO_REQ_STATE_PARSE);

	return req;
}

void
sto_req_init_cb(struct sto_req *req, sto_req_response_t response, void *priv)
{
	req->response = response;
	req->priv = priv;
}

void
sto_req_free(struct sto_req *req)
{
	rte_free(req);
}

void
sto_req_process(struct sto_req *req)
{
	TAILQ_INSERT_TAIL(&g_sto_req_list, req, list);
}

void
sto_req_submit(struct sto_req *req)
{
	sto_req_process(req);
}

static int
sto_req_get_subsystem(struct sto_req *req)
{
	char *subsystem_name = NULL;
	int rc = 0;

	rc = sto_decode_object_str(req->params, "subsystem", &subsystem_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode subystem for req[%p], rc=%d\n", req, rc);
		return rc;
	}

	req->subsystem = sto_subsystem_find(subsystem_name);
	if (spdk_unlikely(!req->subsystem)) {
		SPDK_ERRLOG("Failed to find %s subsystem\n", subsystem_name);
		rc = -ENOENT;
		goto out;
	}

out:
	free(subsystem_name);

	return rc;
}

static void sto_exec_done(void *priv);

static void
sto_req_init_ctx(struct sto_req *req, struct sto_context *ctx)
{
	ctx->priv = req;
	ctx->response = sto_exec_done;
	ctx->err_ctx = &req->err_ctx;

	req->ctx = ctx;
}

static int
sto_req_parse(struct sto_req *req)
{
	struct sto_subsystem *subsystem;
	const struct spdk_json_val *cdb;
	struct sto_context *ctx;
	int rc = 0;

	rc = sto_req_get_subsystem(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to get subsystem for req[%p], rc=%d\n", req, rc);
		return rc;
	}

	subsystem = req->subsystem;

	cdb = sto_decode_next_cdb(req->params);
	if (IS_ERR_OR_NULL(cdb)) {
		SPDK_ERRLOG("Failed to decode CDB for req[%p]\n", req);
		rc = PTR_ERR_OR_ZERO(cdb);
		return rc ?: -EINVAL;
	}

	ctx = subsystem->parse(cdb);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to %s to parse req\n", subsystem->name);
		rc = -EINVAL;
		goto out;
	}

	sto_req_init_ctx(req, ctx);

	sto_req_set_state(req, STO_REQ_STATE_EXEC);
	sto_req_process(req);

out:
	free((struct spdk_json_val *) cdb);

	return rc;
}

static void
sto_exec_done(void *priv)
{
	struct sto_req *req = priv;

	sto_req_set_state(req, STO_REQ_STATE_RESPONSE);
	sto_req_process(req);
}

static int
sto_req_exec(struct sto_req *req)
{
	struct sto_subsystem *subsystem = req->subsystem;

	return subsystem->exec(req->ctx);
}

static void
sto_req_response(struct sto_req *req)
{
	req->response(req);
}

void
sto_req_end_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_subsystem *subsystem = req->subsystem;
	struct sto_err_context *err = &req->err_ctx;

	SPDK_ERRLOG("req[%p] end response: rc=%d\n", req, err->rc);

	if (err->rc) {
		spdk_json_write_object_begin(w);

		spdk_json_write_named_int32(w, "error", err->rc);
		spdk_json_write_named_string(w, "errno_msg", err->errno_msg);

		spdk_json_write_object_end(w);
		goto out;
	}

	subsystem->end_response(req->ctx, w);

out:
	if (req->ctx) {
		subsystem->free(req->ctx);
		req->ctx = NULL;
	}
}

static void
sto_process_req(struct sto_req *req)
{
	int rc = 0;

	switch (req->state) {
	case STO_REQ_STATE_PARSE:
		rc = sto_req_parse(req);
		break;
	case STO_REQ_STATE_EXEC:
		rc = sto_req_exec(req);
		break;
	case STO_REQ_STATE_RESPONSE:
		sto_req_response(req);
		break;
	default:
		SPDK_ERRLOG("req (%p) in state %s, but shouldn't be\n",
			    req, sto_req_state_name(req->state));
		assert(0);
	}

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("req (%p) in state %s failed, rc=%d\n",
			    req, sto_req_state_name(req->state), rc);
		sto_err(&req->err_ctx, rc);
		sto_req_response(req);
	}

	return;
}

int
sto_core_init(void)
{
	g_sto_req_poller = SPDK_POLLER_REGISTER(sto_req_poll, NULL, STO_REQ_POLL_PERIOD);
	if (spdk_unlikely(!g_sto_req_poller)) {
		SPDK_ERRLOG("Cann't register the STO req poller\n");
		return -ENOMEM;
	}

	return 0;
}

void
sto_core_fini(void)
{
	spdk_poller_unregister(&g_sto_req_poller);
}
