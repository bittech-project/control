#include <spdk/thread.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_client.h"

#define STO_CLIENT_POLL_PERIOD	100

static struct spdk_poller *sto_client_poller;

struct sto_client {
	struct spdk_jsonrpc_client *rpc_client;
	bool initialized;
};

static struct sto_client g_sto_client;

static SLIST_HEAD(, sto_rpc_request) g_rpc_req_list = SLIST_HEAD_INITIALIZER(sto_rpc_request);


static struct sto_rpc_request *
_get_rpc_request(int id)
{
	struct sto_rpc_request *req;

	SLIST_FOREACH(req, &g_rpc_req_list, slist) {
		if (req->id == id) {
			return req;
		}
	}

	return NULL;
}

static int
sto_client_poll(void *ctx)
{
	struct sto_client *sto_client = ctx;
	struct spdk_jsonrpc_client_response *resp;
	struct sto_rpc_request *req;
	int rc, id;

	rc = spdk_jsonrpc_client_poll(sto_client->rpc_client, 0);
	if (rc == 0 || rc == -ENOTCONN) {
		/* No response yet */
		return SPDK_POLLER_BUSY;
	}

	if (rc < 0) {
		/* TODO: What should we do? */
		return SPDK_POLLER_BUSY;
	}

	resp = spdk_jsonrpc_client_get_response(sto_client->rpc_client);
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

	SLIST_REMOVE(&g_rpc_req_list, req, sto_rpc_request, slist);

	req->resp_handler(req, resp);

out:
	spdk_jsonrpc_client_free_response(resp);

	return SPDK_POLLER_BUSY;
}

struct sto_rpc_request *
sto_rpc_req_alloc(const char *method_name, sto_dump_params_json params_json, void *priv)
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
	req->priv = priv;
	req->id = ++id;

	return req;
}

void
sto_rpc_req_free(struct sto_rpc_request *req)
{
	free((char *) req->method_name);
	rte_free(req);
}

void
sto_rpc_req_init_cb(struct sto_rpc_request *req, resp_handler resp_handler)
{
	req->resp_handler = resp_handler;
}

int
sto_client_send(struct sto_rpc_request *req)
{
	struct spdk_jsonrpc_client_request *request;
	struct spdk_json_write_ctx *w;

	if (spdk_unlikely(!g_sto_client.initialized)) {
		SPDK_ERRLOG("FAILED: STO client has been failed to initialize\n");
		return -EFAULT;
	}

	request = spdk_jsonrpc_client_create_request();
	if (!request) {
		SPDK_ERRLOG("Failed to create STO rpc request\n");
		return -ENOMEM;
	}

	w = spdk_jsonrpc_begin_request(request, req->id, req->method_name);
	if (!w) {
		spdk_jsonrpc_client_free_request(request);
		return -EFAULT;
	}

	if (req->params_json) {
		spdk_json_write_name(w, "params");

		req->params_json(req, w);

		spdk_jsonrpc_end_request(request, w);
	}

	/* TODO: use a hash table or sorted list */
	SLIST_INSERT_HEAD(&g_rpc_req_list, req, slist);

	spdk_jsonrpc_client_send_request(g_sto_client.rpc_client, request);

	return 0;
}

int
sto_client_connect(const char *addr, int addr_family)
{
	if (g_sto_client.initialized) {
		SPDK_ERRLOG("FAILED: STO client has already been initialized\n");
		return -EINVAL;
	}

	SPDK_NOTICELOG("STO client connect: addr[%s] family[%d]\n",
		       addr, addr_family);

	g_sto_client.rpc_client = spdk_jsonrpc_client_connect(addr, addr_family);
	if (!g_sto_client.rpc_client) {
		SPDK_ERRLOG("spdk_jsonrpc_client_connect() failed: %s\n", spdk_strerror(errno));
		return -errno;
	}

	sto_client_poller = SPDK_POLLER_REGISTER(sto_client_poll, &g_sto_client, STO_CLIENT_POLL_PERIOD);
	if (spdk_unlikely(!sto_client_poller)) {
		SPDK_ERRLOG("Cann't register the STO client poller\n");
		spdk_jsonrpc_client_close(g_sto_client.rpc_client);
		return -EFAULT;
	}

	g_sto_client.initialized = true;

	return 0;
}

void
sto_client_close(void)
{
	if (!g_sto_client.initialized) {
		SPDK_ERRLOG("FAILED: STO client has not been initialized yet\n");
		return;
	}

	spdk_poller_unregister(&sto_client_poller);
	spdk_jsonrpc_client_close(g_sto_client.rpc_client);
	g_sto_client.initialized = false;
}
