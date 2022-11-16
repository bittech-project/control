#include <spdk/thread.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_client.h"

#define STO_CLIENT_MAX_CONNS	64
#define STO_CLIENT_POLL_PERIOD	100

struct sto_client {
	struct spdk_jsonrpc_client *rpc_client;

	TAILQ_ENTRY(sto_client) list;
};

struct sto_client_group {
	const char *addr;
	int addr_family;

	TAILQ_HEAD(, sto_client) free_clients;
	TAILQ_HEAD(, sto_client) clients;

	struct sto_client clients_array[STO_CLIENT_MAX_CONNS];

	struct spdk_poller *req_poller;

	bool initialized;
};

static struct sto_client_group g_sto_client_group;

static TAILQ_HEAD(, sto_rpc_request) g_rpc_req_list = TAILQ_HEAD_INITIALIZER(g_rpc_req_list);
static TAILQ_HEAD(, sto_rpc_request) g_rpc_req_busy_list = TAILQ_HEAD_INITIALIZER(g_rpc_req_busy_list);


static struct sto_rpc_request *
_get_rpc_request(int id)
{
	struct sto_rpc_request *req;

	TAILQ_FOREACH(req, &g_rpc_req_list, list) {
		if (req->id == id) {
			return req;
		}
	}

	return NULL;
}

static int sto_client_send_request(struct sto_client *client, struct sto_rpc_request *req);

static bool
sto_client_check_busy_list(struct sto_client *client)
{
	struct sto_rpc_request *req;
	int rc;

	if (TAILQ_EMPTY(&g_rpc_req_busy_list)) {
		return false;
	}

	req = TAILQ_FIRST(&g_rpc_req_busy_list);
	TAILQ_REMOVE(&g_rpc_req_busy_list, req, list);

	rc = sto_client_send_request(client, req);
	assert(!rc);

	return true;
}

static void
sto_client_poll(struct sto_client_group *cgroup, struct sto_client *client)
{
	struct spdk_jsonrpc_client_response *resp;
	struct sto_rpc_request *req;
	int rc = 0, id;

	rc = spdk_jsonrpc_client_poll(client->rpc_client, 0);
	if (rc == 0 || rc == -ENOTCONN) {
		/* No response yet */
		return;
	}

	if (rc < 0) {
		/* TODO: What should we do? */
		SPDK_ERRLOG("CRIT: GLEB\n");
		return;
	}

	resp = spdk_jsonrpc_client_get_response(client->rpc_client);
	assert(resp);

	/* Check for error response */
	if (resp->error != NULL) {
		SPDK_ERRLOG("Get error response\n");
		goto out;
	}

	rc = spdk_json_decode_int32(resp->id, &id);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode request ID\n");
		goto out;
	}

	SPDK_NOTICELOG("Get response for %d req\n", id);

	req = _get_rpc_request(id);
	assert(req);

	TAILQ_REMOVE(&g_rpc_req_list, req, list);

	req->response_handler(req->priv, resp);

	sto_rpc_req_free(req);

	if (sto_client_check_busy_list(client)) {
		goto out;
	}

	TAILQ_REMOVE(&cgroup->clients, client, list);
	TAILQ_INSERT_HEAD(&cgroup->free_clients, client, list);

out:
	spdk_jsonrpc_client_free_response(resp);
}

static int
sto_client_group_poll(void *ctx)
{
	struct sto_client_group *cgroup = ctx;
	struct sto_client *client, *tmp;

	if (TAILQ_EMPTY(&cgroup->clients)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_FOREACH_SAFE(client, &cgroup->clients, list, tmp) {
		sto_client_poll(cgroup, client);
	}

	return SPDK_POLLER_BUSY;
}

static int
sto_client_send_request(struct sto_client *client, struct sto_rpc_request *req)
{
	struct spdk_jsonrpc_client_request *request;
	struct spdk_json_write_ctx *w;

	request = spdk_jsonrpc_client_create_request();
	if (spdk_unlikely(!request)) {
		SPDK_ERRLOG("Failed to create jsonrpc client request\n");
		return -ENOMEM;
	}

	w = spdk_jsonrpc_begin_request(request, req->id, req->method_name);
	if (!w) {
		spdk_jsonrpc_client_free_request(request);
		return -ENOMEM;
	}

	if (req->params_json) {
		spdk_json_write_name(w, "params");
		req->params_json(req, w);
	}

	spdk_jsonrpc_end_request(request, w);

	/* TODO: use a hash table? */
	TAILQ_INSERT_TAIL(&g_rpc_req_list, req, list);

	spdk_jsonrpc_client_send_request(client->rpc_client, request);

	return 0;
}

static struct sto_rpc_request *
sto_rpc_req_alloc(const char *method_name, sto_dump_params_json params_json)
{
	struct sto_rpc_request *req;
	static int id;

	if (spdk_unlikely(!method_name)) {
		SPDK_ERRLOG("Method name is not set\n");
		return NULL;
	}

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for a RPC req\n");
		return NULL;
	}

	req->method_name = strdup(method_name);
	if (spdk_unlikely(!req->method_name)) {
		SPDK_ERRLOG("Cann't allocate memory for the method name\n");
		rte_free(req);
		return NULL;
	}

	req->params_json = params_json;
	req->id = ++id;

	return req;
}

static void
sto_rpc_req_init_cb(struct sto_rpc_request *req, response_handler_t response_handler, void *priv)
{
	req->response_handler = response_handler;
	req->priv = priv;
}

static void
sto_rpc_req_free(struct sto_rpc_request *req)
{
	free((char *) req->method_name);
	rte_free(req);
}

static int
sto_rpc_req_submit(struct sto_rpc_request *req)
{
	struct sto_client_group *group;
	struct sto_client *client;
	int rc = 0;

	group = &g_sto_client_group;

	if (spdk_unlikely(!group->initialized)) {
		SPDK_ERRLOG("FAILED: STO client has been failed to initialize\n");
		return -EFAULT;
	}

	client = TAILQ_FIRST(&group->free_clients);
	if (!client) {
		TAILQ_INSERT_TAIL(&g_rpc_req_busy_list, req, list);
		goto out;
	}

	rc = sto_client_send_request(client, req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to send STO rpc client request\n");
		return rc;
	}

	TAILQ_REMOVE(&group->free_clients, client, list);
	TAILQ_INSERT_TAIL(&group->clients, client, list);

out:
	return 0;
}

int
sto_client_send(const char *method_name, sto_dump_params_json params_json,
		response_handler_t response_handler, void *priv)
{
	struct sto_rpc_request *req;
	int rc = 0;

	req = sto_rpc_req_alloc(method_name, params_json);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc `%s` RPC req\n", method_name);
		return -ENOMEM;
	}

	sto_rpc_req_init_cb(req, response_handler, priv);

	rc = sto_rpc_req_submit(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to send RPC req, rc=%d\n", rc);
		sto_rpc_req_free(req);
	}

	return rc;
}

static void sto_client_group_close(struct sto_client_group *group);

static int
sto_client_group_connect(struct sto_client_group *group)
{
	int i;

	TAILQ_INIT(&group->free_clients);
	TAILQ_INIT(&group->clients);

	for (i = 0; i < STO_CLIENT_MAX_CONNS; i++) {
		struct spdk_jsonrpc_client *rpc_client;

		rpc_client = spdk_jsonrpc_client_connect(group->addr, group->addr_family);
		if (spdk_unlikely(!rpc_client)) {
			SPDK_ERRLOG("spdk_jsonrpc_client_connect() failed: %s\n", spdk_strerror(errno));
			sto_client_group_close(group);
			return -errno;
		}

		group->clients_array[i].rpc_client = rpc_client;
		TAILQ_INSERT_TAIL(&group->free_clients, &group->clients_array[i], list);
	}

	return 0;
}

static void
sto_client_group_close(struct sto_client_group *group)
{
	int i;

	for (i = 0; i < STO_CLIENT_MAX_CONNS; i++) {
		struct spdk_jsonrpc_client *rpc_client;

		rpc_client = group->clients_array[i].rpc_client;

		if (spdk_unlikely(!rpc_client)) {
			break;
		}

		spdk_jsonrpc_client_close(rpc_client);
	}
}

int
sto_client_connect(const char *addr, int addr_family)
{
	struct sto_client_group *group;
	int rc;

	group = &g_sto_client_group;

	if (group->initialized) {
		SPDK_ERRLOG("FAILED: STO client has already been initialized\n");
		return -EINVAL;
	}

	SPDK_NOTICELOG("STO client connect: addr[%s] family[%d]\n",
		       addr, addr_family);

	memset(group, 0, sizeof(*group));

	group->addr = strdup(addr);
	if (spdk_unlikely(!group->addr)) {
		SPDK_ERRLOG("Cannot allocate memory for addr %s\n", addr);
		return -ENOMEM;
	}

	group->addr_family = addr_family;

	rc = sto_client_group_connect(group);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to connect client group: rc=%d\n", rc);
		goto free_addr;
	}

	group->req_poller = SPDK_POLLER_REGISTER(sto_client_group_poll, group, STO_CLIENT_POLL_PERIOD);
	if (spdk_unlikely(!group->req_poller)) {
		SPDK_ERRLOG("Cann't register the STO client poller\n");
		rc = -ENOMEM;
		goto close_clients;
	}

	group->initialized = true;

	return 0;

close_clients:
	sto_client_group_close(group);

free_addr:
	free((char *) group->addr);

	return rc;
}

void
sto_client_close(void)
{
	struct sto_client_group *group;

	group = &g_sto_client_group;

	if (!group->initialized) {
		SPDK_ERRLOG("FAILED: STO client has not been initialized yet\n");
		return;
	}

	spdk_poller_unregister(&group->req_poller);
	sto_client_group_close(group);
	free((char *) group->addr);

	group->initialized = false;
}
