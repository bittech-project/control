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

	struct spdk_poller *poller;

	bool initialized;
};

static struct sto_client_group g_sto_client_group;

struct sto_rpc_cmd {
	int id;
	TAILQ_ENTRY(sto_rpc_cmd) list;

	struct spdk_jsonrpc_client_request *request;

	void *priv;
	sto_client_response_handler_t response_handler;
};

static TAILQ_HEAD(, sto_rpc_cmd) g_rpc_cmd_list = TAILQ_HEAD_INITIALIZER(g_rpc_cmd_list);
static TAILQ_HEAD(, sto_rpc_cmd) g_rpc_cmd_busy_list = TAILQ_HEAD_INITIALIZER(g_rpc_cmd_busy_list);


static struct sto_rpc_cmd *
_get_rpc_cmd(int id)
{
	struct sto_rpc_cmd *cmd;

	TAILQ_FOREACH(cmd, &g_rpc_cmd_list, list) {
		if (cmd->id == id) {
			return cmd;
		}
	}

	return NULL;
}

static void sto_client_submit_cmd(struct sto_client *client, struct sto_rpc_cmd *cmd);

static bool
sto_client_check_busy_list(struct sto_client *client)
{
	struct sto_rpc_cmd *cmd;

	if (TAILQ_EMPTY(&g_rpc_cmd_busy_list)) {
		return false;
	}

	cmd = TAILQ_FIRST(&g_rpc_cmd_busy_list);
	TAILQ_REMOVE(&g_rpc_cmd_busy_list, cmd, list);

	sto_client_submit_cmd(client, cmd);

	return true;
}

struct json_write_buf {
	char data[1024];
	unsigned cur_off;
};

static int
__json_write_stdout(void *cb_ctx, const void *data, size_t size)
{
	struct json_write_buf *buf = cb_ctx;
	size_t rc;

	rc = snprintf(buf->data + buf->cur_off, sizeof(buf->data) - buf->cur_off,
		      "%s", (const char *)data);
	if (rc > 0) {
		buf->cur_off += rc;
	}
	return rc == size ? 0 : -1;
}

static void sto_rpc_cmd_free(struct sto_rpc_cmd *cmd);

static void
sto_client_response(struct spdk_jsonrpc_client_response *response)
{
	struct sto_rpc_cmd *cmd;
	int id, rc;

	rc = spdk_json_decode_int32(response->id, &id);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("CRITICAL: Failed to decode RPC cmd ID, rc=%d\n!!!", rc);
		return;
	}

	SPDK_NOTICELOG("Got response for %d RPC cmd\n", id);

	/* Check for error response */
	if (response->error != NULL) {
		struct json_write_buf buf = {};
		struct spdk_json_write_ctx *w = spdk_json_write_begin(__json_write_stdout,
						&buf, SPDK_JSON_PARSE_FLAG_DECODE_IN_PLACE);

		if (w == NULL) {
			SPDK_ERRLOG("error response: (?)\n");
		} else {
			spdk_json_write_val(w, response->error);
			spdk_json_write_end(w);
			SPDK_ERRLOG("error response: \n%s\n", buf.data);
		}
		rc = -EFAULT;
	}

	cmd = _get_rpc_cmd(id);
	assert(cmd);

	TAILQ_REMOVE(&g_rpc_cmd_list, cmd, list);

	cmd->response_handler(cmd->priv, response, rc);

	sto_rpc_cmd_free(cmd);
}

static void
sto_client_poll(struct sto_client_group *cgroup, struct sto_client *client)
{
	struct spdk_jsonrpc_client_response *response;
	int rc = 0;

	rc = spdk_jsonrpc_client_poll(client->rpc_client, 0);
	if (rc == 0 || rc == -ENOTCONN) {
		/* No response yet */
		return;
	}

	if (rc < 0) {
		/* TODO: What should we do? */
		SPDK_ERRLOG("CRITICAL: spdk_jsonrpc_client_poll return ERROR, rc=%d\n", rc);
		return;
	}

	response = spdk_jsonrpc_client_get_response(client->rpc_client);
	assert(response);

	sto_client_response(response);

	spdk_jsonrpc_client_free_response(response);

	if (sto_client_check_busy_list(client)) {
		return;
	}

	TAILQ_REMOVE(&cgroup->clients, client, list);
	TAILQ_INSERT_HEAD(&cgroup->free_clients, client, list);
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

static struct spdk_jsonrpc_client_request *
sto_client_alloc_request(const char *method_name, int id,
			 void *params, sto_client_dump_json_params_t dump_json_params)
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

	if (dump_json_params) {
		spdk_json_write_name(w, "params");
		dump_json_params(params, w);
	}

	spdk_jsonrpc_end_request(request, w);

	return request;
}

static void
sto_client_submit_cmd(struct sto_client *client, struct sto_rpc_cmd *cmd)
{
	/* TODO: use a hash table? */
	TAILQ_INSERT_TAIL(&g_rpc_cmd_list, cmd, list);

	spdk_jsonrpc_client_send_request(client->rpc_client, cmd->request);
	cmd->request = NULL;
}

static struct sto_rpc_cmd *
sto_rpc_cmd_alloc(void)
{
	struct sto_rpc_cmd *cmd;
	static int id;

	cmd = rte_zmalloc(NULL, sizeof(*cmd), 0);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for a RPC cmd\n");
		return NULL;
	}

	cmd->id = ++id;

	return cmd;
}

static void
sto_rpc_cmd_init_cb(struct sto_rpc_cmd *cmd,
		    sto_client_response_handler_t response_handler, void *priv)
{
	cmd->response_handler = response_handler;
	cmd->priv = priv;
}

static void
sto_rpc_cmd_free(struct sto_rpc_cmd *cmd)
{
	if (cmd->request) {
		spdk_jsonrpc_client_free_request(cmd->request);
	}

	rte_free(cmd);
}

static void
sto_rpc_cmd_run(struct sto_rpc_cmd *cmd)
{
	struct sto_client_group *group;
	struct sto_client *client;

	group = &g_sto_client_group;

	client = TAILQ_FIRST(&group->free_clients);
	if (!client) {
		TAILQ_INSERT_TAIL(&g_rpc_cmd_busy_list, cmd, list);
		return;
	}

	sto_client_submit_cmd(client, cmd);

	TAILQ_REMOVE(&group->free_clients, client, list);
	TAILQ_INSERT_TAIL(&group->clients, client, list);

	return;
}

int
sto_client_send(const char *method_name, void *params,
		sto_client_dump_json_params_t dump_json_params,
		struct sto_client_args *args)
{
	struct sto_client_group *group;
	struct sto_rpc_cmd *cmd;
	int rc = 0;

	group = &g_sto_client_group;

	if (spdk_unlikely(!group->initialized)) {
		SPDK_ERRLOG("FAILED: STO client has been failed to initialize\n");
		return -EFAULT;
	}

	if (spdk_unlikely(!method_name)) {
		SPDK_ERRLOG("Method name is not set\n");
		return -EINVAL;
	}

	cmd = sto_rpc_cmd_alloc();
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to alloc `%s` RPC cmd\n", method_name);
		return -ENOMEM;
	}

	cmd->request = sto_client_alloc_request(method_name, cmd->id, params, dump_json_params);
	if (spdk_unlikely(!cmd->request)) {
		SPDK_ERRLOG("Failed to create jsonrpc client request\n");
		rc = -ENOMEM;
		goto free_cmd;
	}

	sto_rpc_cmd_init_cb(cmd, args->response_handler, args->priv);
	sto_rpc_cmd_run(cmd);

	return 0;

free_cmd:
	sto_rpc_cmd_free(cmd);

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

	group->poller = SPDK_POLLER_REGISTER(sto_client_group_poll, group, STO_CLIENT_POLL_PERIOD);
	if (spdk_unlikely(!group->poller)) {
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

	spdk_poller_unregister(&group->poller);
	sto_client_group_close(group);
	free((char *) group->addr);

	group->initialized = false;
}
