#include <spdk/thread.h>
#include <spdk/json.h>
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
sto_req_alloc(const struct spdk_json_val *cdb)
{
	struct sto_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for req\n");
		return NULL;
	}

	req->cdb = cdb;
	sto_req_set_state(req, STO_REQ_STATE_PARSE);

	return req;
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

int
sto_req_cdb_decode_str(struct sto_req *req, const char *name, char **value)
{
	const struct spdk_json_val *cdb = req->cdb;
	const struct spdk_json_val *name_json, *value_json;
	uint32_t offset;
	int rc = 0;

	if (!cdb || cdb->type != SPDK_JSON_VAL_OBJECT_BEGIN) {
		SPDK_ERRLOG("req[%p] has wrong CDB\n", req);
		return -EINVAL;
	}

	offset = req->cdb_offset;

	name_json = &cdb[offset + 1];
	if (!spdk_json_strequal(name_json, name)) {
		SPDK_ERRLOG("req[%p] doesn't have '%s' object\n", req, name);
		return -EINVAL;
	}

	value_json = &cdb[offset + 2];

	if (spdk_json_decode_string(value_json, value)) {
		SPDK_ERRLOG("req[%p] Failed to decode '%s' field\n", req, name);
		return -EINVAL;
	}

	SPDK_NOTICELOG("Parse `%s` string from req[%p] CDB\n", *value, req);

	req->cdb_offset += 1 + spdk_json_val_len(value_json);

	return rc;
}

static int
sto_req_parse(struct sto_req *req)
{
	char *subsystem = NULL;
	int rc;

	rc = sto_req_cdb_decode_str(req, "subsystem", &subsystem);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to define subsystem for req[%p], rc=%d\n", req, rc);
		return rc;
	}

	req->subsystem = sto_subsystem_find(subsystem);
	if (spdk_unlikely(!req->subsystem)) {
		SPDK_ERRLOG("Failed to find %s subsystem\n", subsystem);
		rc = -EINVAL;
		goto out;
	}

	rc = req->subsystem->parse(req);

out:
	free(subsystem);

	return rc;
}

static int
sto_req_exec(struct sto_req *req)
{
	int rc;

	rc = req->subsystem->exec(req);

	return rc;
}

static void
sto_req_done(struct sto_req *req)
{
	req->subsystem->done(req);

	req->req_done(req);

	return;
}

static int
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
	case STO_REQ_STATE_DONE:
		sto_req_done(req);
		break;
	default:
		SPDK_ERRLOG("req (%p) in state %s, but shouldn't be\n",
				req, sto_req_state_name(req->state));
		assert(0);
	}

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("req (%p) in state %s failed, rc=%d\n",
				req, sto_req_state_name(req->state), rc);
		req->req_done(req);
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
