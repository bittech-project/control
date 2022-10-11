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
sto_req_init_cb(struct sto_req *req, sto_req_done_t req_done, void *priv)
{
	req->req_done = req_done;
	req->priv = priv;
}

void
sto_req_free(struct sto_req *req)
{
	free((struct spdk_json_val *) req->cdb);
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

struct sto_response *
sto_response_alloc(int resultcode, const char *fmt, ...)
{
	struct sto_response *resp;
	va_list args;

	resp = rte_zmalloc(NULL, sizeof(*resp), 0);
	if (spdk_unlikely(!resp)) {
		SPDK_ERRLOG("Failed to alloc STO response\n");
		return NULL;
	}

	resp->resultcode = resultcode;

	if (!fmt) {
		goto out;
	}

	va_start(args, fmt);
	resp->buf = spdk_vsprintf_alloc(fmt, args);
	va_end(args);

	if (spdk_unlikely(!resp->buf)) {
		SPDK_ERRLOG("Failed to alloc STO response buf\n");
		rte_free(resp);
		return NULL;
	}

out:
	return resp;
}

void
sto_response_free(struct sto_response *resp)
{
	free(resp->buf);
	rte_free(resp);
}

void
sto_response_dump_json(struct sto_response *resp, struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "result", resp->resultcode);
	spdk_json_write_named_string(w, "buf", resp->buf ?: "");

	spdk_json_write_object_end(w);
}

static int
sto_decode_object_str(const struct spdk_json_val *values,
		      const char *name, char **value)
{
	const struct spdk_json_val *name_json, *value_json;

	name_json = &values[0];
	if (!spdk_json_strequal(name_json, name)) {
		SPDK_ERRLOG("JSON object name doesn't correspond to %s\n", name);
		return -ENOENT;
	}

	value_json = &values[1];

	if (spdk_json_decode_string(value_json, value)) {
		SPDK_ERRLOG("Failed to decode string from JSON object %s\n", name);
		return -EDOM;
	}

	return 1 + spdk_json_val_len(value_json);
}

const struct spdk_json_val *
sto_decode_cdb(const struct spdk_json_val *params, const char *name, char **value)
{
	struct spdk_json_val *cdb;
	uint32_t cdb_len, size;
	int res = 0;
	int i;

	if (!params || params->type != SPDK_JSON_VAL_OBJECT_BEGIN || !params->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", params);
		return ERR_PTR(-EINVAL);
	}

	SPDK_NOTICELOG("Start parse JSON for CDB: params_len=%u\n", params->len);

	res = sto_decode_object_str(params + 1, name, value);
	if (res < 0) {
		SPDK_ERRLOG("Failed to decode CDB\n");
		return ERR_PTR(res);
	}

	SPDK_NOTICELOG("Parse `%s` string from params\n", *value);

	cdb_len = params->len - res;
	if (!cdb_len) {
		SPDK_NOTICELOG("CDB len is equal zero: offset=%u params_len=%u\n",
			       res, params->len);
		return NULL;
	}

	size = cdb_len + 2;

	cdb = calloc(size, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!cdb)) {
		SPDK_ERRLOG("Failed to alloc CDB: size=%u\n", size);
		return ERR_PTR(-ENOMEM);
	}

	cdb->type = SPDK_JSON_VAL_OBJECT_BEGIN;
	cdb->len = cdb_len;

	for (i = 1; i <= cdb_len + 1; i++) {
		cdb[i].start = params[i + res].start;
		cdb[i].len = params[i + res].len;
		cdb[i].type = params[i + res].type;
	}

	return cdb;
}

static int
sto_req_get_subsystem(struct sto_req *req)
{
	char *subsystem_name = NULL;
	int rc = 0;

	req->cdb = sto_decode_cdb(req->params, "subsystem", &subsystem_name);
	if (IS_ERR_OR_NULL(req->cdb)) {
		SPDK_ERRLOG("Failed to decode CDB for req[%p]\n", req);
		rc = PTR_ERR_OR_ZERO(req->cdb);
		return rc ?: -EINVAL;
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

static int
sto_req_parse(struct sto_req *req)
{
	struct sto_subsystem *subsystem;
	int rc = 0;

	rc = sto_req_get_subsystem(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to get subsystem for req[%p], rc=%d\n", req, rc);
		return rc;
	}

	subsystem = req->subsystem;

	req->subsys_req = subsystem->alloc_req(req->cdb);
	if (spdk_unlikely(!req->subsys_req)) {
		SPDK_ERRLOG("Failed to alloc %s req\n", subsystem->name);
		return -EINVAL;
	}

	sto_req_set_state(req, STO_REQ_STATE_EXEC);
	sto_req_process(req);

	return 0;
}

static void
sto_exec_done(void *arg, struct sto_response *resp)
{
	struct sto_req *req = arg;

	req->resp = resp;

	sto_req_set_state(req, STO_REQ_STATE_DONE);
	sto_req_process(req);
}

static int
sto_req_exec(struct sto_req *req)
{
	struct sto_subsystem *subsystem = req->subsystem;
	void *subsys_req = req->subsys_req;

	return subsystem->exec_req(subsys_req, sto_exec_done, req);
}

static void
sto_req_done(struct sto_req *req)
{
	struct sto_subsystem *subsystem = req->subsystem;

	subsystem->done_req(req->subsys_req);
	req->subsys_req = NULL;

	req->req_done(req);

	return;
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
	case STO_REQ_STATE_DONE:
		sto_req_done(req);
		break;
	default:
		SPDK_ERRLOG("req (%p) in state %s, but shouldn't be\n",
			    req, sto_req_state_name(req->state));
		assert(0);
	}

	if (spdk_unlikely(rc)) {
		struct sto_response *resp = sto_response_alloc(rc, NULL);
		req->resp = resp;

		SPDK_ERRLOG("req (%p) in state %s failed, rc=%d\n",
			    req, sto_req_state_name(req->state), rc);
		req->req_done(req);
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
