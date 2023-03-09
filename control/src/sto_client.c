#include "sto_client.h"

#include <spdk/stdinc.h>
#include <spdk/thread.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>
#include <spdk/json.h>
#include <spdk/jsonrpc.h>
#include <spdk/queue.h>
#include <spdk/util.h>

#include "sto_json.h"
#include "sto_err.h"
#include "sto_hash.h"

#define STO_JSONRPC_CLIENT_MAX_CONNS	64
#define STO_JSONRPC_CLIENT_POLL_PERIOD	100

struct sto_jsonrpc_client_entry {
	struct spdk_jsonrpc_client *rpc_client;
	TAILQ_ENTRY(sto_jsonrpc_client_entry) list;
};

struct sto_jsonrpc_client {
	const char *addr;
	int addr_family;

	struct sto_jsonrpc_client_entry entries_arr[STO_JSONRPC_CLIENT_MAX_CONNS];

	TAILQ_HEAD(, sto_jsonrpc_client_entry) free_entries;
	TAILQ_HEAD(, sto_jsonrpc_client_entry) entries;

	int req_id;
	struct sto_hash req_map;

	TAILQ_HEAD(, sto_jsonrpc_client_req) req_busy_list;

	struct spdk_poller *poller;
};

struct sto_jsonrpc_client_req {
	int id;

	struct sto_hash_elem he;
	TAILQ_ENTRY(sto_jsonrpc_client_req) list;

	struct spdk_jsonrpc_client_request *request;

	void *priv;
	sto_client_response_handler_t response_handler;
};

static struct sto_jsonrpc_client *g_sto_jsonrpc_client;

static inline int
jsonrpc_client_next_id(struct sto_jsonrpc_client *client)
{
	return client->req_id == INT_MAX ? 0 : client->req_id + 1;
}

static void
__jsonrpc_client_send_req(struct sto_jsonrpc_client *client,
			  struct sto_jsonrpc_client_entry *entry,
			  struct sto_jsonrpc_client_req *req)
{
	sto_hash_add(&client->req_map, &req->he);

	spdk_jsonrpc_client_send_request(entry->rpc_client, req->request);
	req->request = NULL;
}

static bool
jsonrpc_client_check_busy_list(struct sto_jsonrpc_client *client,
			       struct sto_jsonrpc_client_entry *entry)
{
	struct sto_jsonrpc_client_req *req;

	if (TAILQ_EMPTY(&client->req_busy_list)) {
		return false;
	}

	req = TAILQ_FIRST(&client->req_busy_list);
	TAILQ_REMOVE(&client->req_busy_list, req, list);

	__jsonrpc_client_send_req(client, entry, req);

	return true;
}

static void jsonrpc_client_free_req(struct sto_jsonrpc_client_req *req);

static inline struct sto_jsonrpc_client_req *
jsonrpc_client_get_req(struct sto_jsonrpc_client *client, int id)
{
	struct sto_hash_elem *he;

	he = sto_hash_lookup(&client->req_map, &id, sizeof(id));
	if (!he) {
		return NULL;
	}

	return SPDK_CONTAINEROF(he, struct sto_jsonrpc_client_req, he);
}

static void
jsonrpc_client_response(struct sto_jsonrpc_client *client,
			struct spdk_jsonrpc_client_response *response)
{
	struct sto_jsonrpc_client_req *req;
	int id, rc;

	rc = spdk_json_decode_int32(response->id, &id);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("CRITICAL: Failed to decode RPC req ID, rc=%d\n!!!", rc);
		return;
	}

	/* Check for error response */
	if (response->error != NULL) {
		sto_json_print("Client response error", response->error);
		rc = -EFAULT;
	}

	req = jsonrpc_client_get_req(client, id);
	assert(req);

	sto_hash_elem_del(&req->he);

	req->response_handler(req->priv, response, rc);

	jsonrpc_client_free_req(req);
}

static void
jsonrpc_client_check_response(struct sto_jsonrpc_client *client,
			      struct sto_jsonrpc_client_entry *entry)
{
	struct spdk_jsonrpc_client_response *response;
	int rc = 0;

	rc = spdk_jsonrpc_client_poll(entry->rpc_client, 0);
	if (rc == 0 || rc == -ENOTCONN) {
		/* No response yet */
		return;
	}

	if (rc < 0) {
		SPDK_ERRLOG("CRITICAL: spdk_jsonrpc_client_poll return ERROR, rc=%d\n", rc);
		/* TODO: What should we do? */
		assert(false);
		return;
	}

	response = spdk_jsonrpc_client_get_response(entry->rpc_client);
	assert(response);

	jsonrpc_client_response(client, response);

	spdk_jsonrpc_client_free_response(response);

	if (jsonrpc_client_check_busy_list(client, entry)) {
		return;
	}

	TAILQ_REMOVE(&client->entries, entry, list);
	TAILQ_INSERT_HEAD(&client->free_entries, entry, list);
}

static int
jsonrpc_client_poll(void *ctx)
{
	struct sto_jsonrpc_client *client = ctx;
	struct sto_jsonrpc_client_entry *entry, *tmp;

	if (TAILQ_EMPTY(&client->entries)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_FOREACH_SAFE(entry, &client->entries, list, tmp) {
		jsonrpc_client_check_response(client, entry);
	}

	return SPDK_POLLER_BUSY;
}

static struct spdk_jsonrpc_client_request *
__alloc_jsonrpc_client_request(const char *method_name, int id,
			       void *params, sto_client_dump_params_t dump_params)
{
	struct spdk_jsonrpc_client_request *request;
	struct spdk_json_write_ctx *w;

	request = spdk_jsonrpc_client_create_request();
	if (spdk_unlikely(!request)) {
		SPDK_ERRLOG("Failed to create jsonrpc client request\n");
		return NULL;
	}

	w = spdk_jsonrpc_begin_request(request, id, method_name);
	if (!w) {
		spdk_jsonrpc_client_free_request(request);
		return NULL;
	}

	if (dump_params) {
		spdk_json_write_name(w, "params");
		dump_params(params, w);
	}

	spdk_jsonrpc_end_request(request, w);

	return request;
}

static struct sto_jsonrpc_client_req *
jsonrpc_client_create_req(struct sto_jsonrpc_client *client,
			  const char *method_name, void *params,
			  sto_client_dump_params_t dump_params)
{
	struct sto_jsonrpc_client_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for a RPC req\n");
		return NULL;
	}

	req->id = client->req_id = jsonrpc_client_next_id(client);

	sto_hash_elem_init(&req->he, &req->id, sizeof(req->id));

	req->request = __alloc_jsonrpc_client_request(method_name, req->id, params, dump_params);
	if (spdk_unlikely(!req->request)) {
		SPDK_ERRLOG("Failed to create jsonrpc client request\n");
		goto free_req;
	}

	return req;

free_req:
	free(req);

	return NULL;
}

static void
jsonrpc_client_req_init_cb(struct sto_jsonrpc_client_req *req,
			   sto_client_response_handler_t response_handler, void *priv)
{
	req->response_handler = response_handler;
	req->priv = priv;
}

static void
jsonrpc_client_free_req(struct sto_jsonrpc_client_req *req)
{
	free(req);
}

static void
jsonrpc_client_send_req(struct sto_jsonrpc_client *client,
			struct sto_jsonrpc_client_req *req)
{
	struct sto_jsonrpc_client_entry *entry;

	entry = TAILQ_FIRST(&client->free_entries);
	if (!entry) {
		TAILQ_INSERT_TAIL(&client->req_busy_list, req, list);
		return;
	}

	__jsonrpc_client_send_req(client, entry, req);

	TAILQ_REMOVE(&client->free_entries, entry, list);
	TAILQ_INSERT_TAIL(&client->entries, entry, list);
}

int
sto_client_send(const char *method_name, void *params,
		sto_client_dump_params_t dump_params,
		struct sto_client_args *args)
{
	struct sto_jsonrpc_client *client;
	struct sto_jsonrpc_client_req *req;

	if (spdk_unlikely(!method_name)) {
		SPDK_ERRLOG("Method name is not set\n");
		return -EINVAL;
	}

	client = g_sto_jsonrpc_client;

	req = jsonrpc_client_create_req(client, method_name, params, dump_params);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc `%s` RPC req\n", method_name);
		return -ENOMEM;
	}

	jsonrpc_client_req_init_cb(req, args->response_handler, args->priv);

	jsonrpc_client_send_req(client, req);

	return 0;
}

static void jsonrpc_client_close(struct sto_jsonrpc_client *client);

static int
jsonrpc_client_connect(struct sto_jsonrpc_client *client)
{
	int i;

	TAILQ_INIT(&client->free_entries);
	TAILQ_INIT(&client->entries);

	for (i = 0; i < STO_JSONRPC_CLIENT_MAX_CONNS; i++) {
		struct sto_jsonrpc_client_entry *entry = &client->entries_arr[i];

		entry->rpc_client = spdk_jsonrpc_client_connect(client->addr, client->addr_family);
		if (spdk_unlikely(!entry->rpc_client)) {
			SPDK_ERRLOG("spdk_jsonrpc_client_connect() failed: %s\n", spdk_strerror(errno));
			jsonrpc_client_close(client);
			return -errno;
		}

		TAILQ_INSERT_TAIL(&client->free_entries, entry, list);
	}

	return 0;
}

static void
jsonrpc_client_close(struct sto_jsonrpc_client *client)
{
	int i;

	for (i = 0; i < STO_JSONRPC_CLIENT_MAX_CONNS; i++) {
		struct sto_jsonrpc_client_entry *entry = &client->entries_arr[i];

		if (spdk_unlikely(!entry->rpc_client)) {
			break;
		}

		spdk_jsonrpc_client_close(entry->rpc_client);
	}
}

static struct sto_jsonrpc_client *
sto_jsonrpc_client_connect(const char *addr, int addr_family)
{
	struct sto_jsonrpc_client *client;
	int rc;

	client = calloc(1, sizeof(*client));
	if (spdk_unlikely(!client)) {
		SPDK_ERRLOG("Failed to alloc jsonrpc client: addr[%s] family[%d]\n",
			    addr, addr_family);
		return ERR_PTR(-ENOMEM);
	}

	client->addr = strdup(addr);
	if (spdk_unlikely(!client->addr)) {
		SPDK_ERRLOG("Cannot allocate memory for addr %s\n", addr);
		return ERR_PTR(-ENOMEM);
	}

	client->addr_family = addr_family;

	rc = sto_hash_init(&client->req_map, STO_JSONRPC_CLIENT_MAX_CONNS);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create cmd map\n");
		rc = -ENOMEM;
		goto free_addr;
	}

	TAILQ_INIT(&client->req_busy_list);

	rc = jsonrpc_client_connect(client);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to connect jsonrpc client: rc=%d\n", rc);
		goto free_req_map;
	}

	client->poller = SPDK_POLLER_REGISTER(jsonrpc_client_poll, client, STO_JSONRPC_CLIENT_POLL_PERIOD);
	if (spdk_unlikely(!client->poller)) {
		SPDK_ERRLOG("Cann't register the STO client poller\n");
		rc = -ENOMEM;
		goto close_client;
	}

	SPDK_NOTICELOG("STO jsonrpc client connect: addr[%s] family[%d]\n",
		       client->addr, client->addr_family);

	return client;

close_client:
	jsonrpc_client_close(client);

free_req_map:
	sto_hash_destroy(&client->req_map);

free_addr:
	free((char *) client->addr);

	return ERR_PTR(rc);
}

static void
sto_jsonrpc_client_close(struct sto_jsonrpc_client *client)
{
	spdk_poller_unregister(&client->poller);
	jsonrpc_client_close(client);
	sto_hash_destroy(&client->req_map);
	free((char *) client->addr);
	free(client);
}

int
sto_client_connect(const char *addr, int addr_family)
{
	struct sto_jsonrpc_client *client;

	if (g_sto_jsonrpc_client) {
		SPDK_ERRLOG("FAILED: STO client has already been initialized\n");
		return -EINVAL;
	}

	client = sto_jsonrpc_client_connect(addr, addr_family);
	if (IS_ERR(client)) {
		SPDK_ERRLOG("Failed to alloc STO jsonrpc client\n");
		return PTR_ERR(client);
	}

	g_sto_jsonrpc_client = client;

	return 0;
}

void
sto_client_close(void)
{
	struct sto_jsonrpc_client *client = g_sto_jsonrpc_client;

	if (!client) {
		SPDK_ERRLOG("FAILED: STO client has not been initialized yet\n");
		return;
	}

	sto_jsonrpc_client_close(client);
}
