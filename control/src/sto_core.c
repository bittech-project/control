#include <spdk/thread.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_core.h"
#include "sto_subsystem.h"

#define STO_REQ_POLL_PERIOD	1000 /* 1ms */

struct spdk_poller *g_sto_req_poller;

static TAILQ_HEAD(sto_req_list, sto_req) g_sto_req_list
	= TAILQ_HEAD_INITIALIZER(g_sto_req_list);


static int sto_process_req(struct sto_req *req);

static const char *const sto_req_state_names[] = {
	[STO_REQ_STATE_PARSE]	= "STATE_PARSE",
	[STO_REQ_STATE_EXEC]	= "STATE_EXEC",
	[STO_REQ_STATE_DONE]	= "STATE_DONE",
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
	int rc = 0;

	if (TAILQ_EMPTY(&g_sto_req_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&req_list, &g_sto_req_list, sto_req, list);

	TAILQ_FOREACH_SAFE(req, &req_list, list, tmp) {
		TAILQ_REMOVE(&req_list, req, list);

		rc = sto_process_req(req);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to process req, rc=%d\n", rc);
		}
	}

	return SPDK_POLLER_BUSY;
}

struct sto_req *
sto_req_alloc(const char *subsystem)
{
	struct sto_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for req\n");
		return NULL;
	}

	req->subsystem = sto_subsystem_find(subsystem);
	if (spdk_unlikely(!req->subsystem)) {
		SPDK_ERRLOG("Failed to find %s susbsytem\n", subsystem);
		goto free_req;
	}

	sto_req_set_state(req, STO_REQ_STATE_PARSE);

	return req;

free_req:
	rte_free(req);

	return NULL;
}

void
sto_req_init_cb(struct sto_req *req, sto_req_done_t req_done, void *priv)
{
	req->req_done = req_done;
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

int
sto_req_submit(struct sto_req *req)
{
	sto_req_process(req);

	return 0;
}

static int
sto_req_parse(struct sto_req *req)
{
	int rc;

	rc = req->subsystem->parse(req);

	return rc;
}

static int
sto_req_exec(struct sto_req *req)
{
	int rc;

	rc = req->subsystem->exec(req);

	return rc;
}

static int
sto_req_done(struct sto_req *req)
{
	int rc;

	rc = req->subsystem->done(req);

	req->req_done(req);

	return rc;
}

static int
sto_process_req(struct sto_req *req)
{
	int rc;

	switch (req->state) {
	case STO_REQ_STATE_PARSE:
		rc = sto_req_parse(req);
		break;
	case STO_REQ_STATE_EXEC:
		rc = sto_req_exec(req);
		break;
	case STO_REQ_STATE_DONE:
		rc = sto_req_done(req);
		break;
	default:
		SPDK_ERRLOG("req (%p) in state %s, but shouldn't be\n",
				req, sto_req_state_name(req->state));
		assert(0);
	}

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("req (%p) in state %s failed, rc=%d\n",
				req, sto_req_state_name(req->state), rc);
	}

	return 0;
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
