#include <spdk/thread.h>
#include <spdk/log.h>
#include <spdk/likely.h>

#include <rte_malloc.h>

#include "sto_core.h"
#include "sto_subsystem.h"

#define STO_REQ_POLL_PERIOD	100

struct spdk_poller *g_sto_req_poller;

static TAILQ_HEAD(, sto_req) g_sto_req_list
	= TAILQ_HEAD_INITIALIZER(g_sto_req_list);

int sto_process_req(struct sto_req *req);

static int
sto_req_poll(void *ctx)
{
	struct sto_req *req, *tmp;
	int rc = 0;

	if (TAILQ_EMPTY(&g_sto_req_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_FOREACH_SAFE(req, &g_sto_req_list, list, tmp) {
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

int
sto_req_submit(struct sto_req *req)
{
	TAILQ_INSERT_TAIL(&g_sto_req_list, req, list);

	return 0;
}

int sto_process_req(struct sto_req *req)
{
	TAILQ_REMOVE(&g_sto_req_list, req, list);

	req->req_done(req);

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
