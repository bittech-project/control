#include <spdk/thread.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_utils.h"
#include "sto_client.h"
#include "sto_err.h"
#include "sto_hash.h"

#define STO_CLIENT_MAX_CONNS	64
#define STO_CLIENT_POLL_PERIOD	100

struct sto_rpc_cmd;

struct sto_client {
	struct spdk_jsonrpc_client *rpc_client;
	TAILQ_ENTRY(sto_client) list;
};

struct sto_client_group {
	const char *addr;
	int addr_family;

	struct sto_client clients_array[STO_CLIENT_MAX_CONNS];

	TAILQ_HEAD(, sto_client) free_clients;
	TAILQ_HEAD(, sto_client) clients;

	int cmd_id;
	struct sto_hash *cmd_map;
	TAILQ_HEAD(, sto_rpc_cmd) cmd_busy_list;

	struct spdk_poller *poller;
};

struct sto_rpc_cmd {
	int id;
	TAILQ_ENTRY(sto_rpc_cmd) list;
	struct sto_hash_elem he;

	struct spdk_jsonrpc_client_request *request;

	void *priv;
	sto_client_response_handler_t response_handler;
};

static struct sto_client_group *g_sto_client_group;

static inline int
sto_client_group_next_cmd_id(struct sto_client_group *group)
{
	return group->cmd_id == INT_MAX ? 0 : group->cmd_id + 1;
}

static void sto_client_group_submit_cmd(struct sto_client_group *group, struct sto_client *client, struct sto_rpc_cmd *cmd);

static bool
sto_client_group_check_busy_list(struct sto_client_group *group, struct sto_client *client)
{
	struct sto_rpc_cmd *cmd;

	if (TAILQ_EMPTY(&group->cmd_busy_list)) {
		return false;
	}

	cmd = TAILQ_FIRST(&group->cmd_busy_list);
	TAILQ_REMOVE(&group->cmd_busy_list, cmd, list);

	sto_client_group_submit_cmd(group, client, cmd);

	return true;
}

static void sto_rpc_cmd_free(struct sto_rpc_cmd *cmd);

static void
sto_client_group_response(struct sto_client_group *group, struct spdk_jsonrpc_client_response *response)
{
	struct sto_rpc_cmd *cmd;
	int id, rc;

	rc = spdk_json_decode_int32(response->id, &id);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("CRITICAL: Failed to decode RPC cmd ID, rc=%d\n!!!", rc);
		return;
	}

	/* Check for error response */
	if (response->error != NULL) {
		sto_json_print(response->error);
		rc = -EFAULT;
	}

	cmd = sto_hash_lookup(group->cmd_map, &id, sizeof(id));
	assert(cmd);

	sto_hash_remove_elem(&cmd->he);

	cmd->response_handler(cmd->priv, response, rc);

	sto_rpc_cmd_free(cmd);
}

static void
sto_client_group_check_response(struct sto_client_group *group, struct sto_client *client)
{
	struct spdk_jsonrpc_client_response *response;
	int rc = 0;

	rc = spdk_jsonrpc_client_poll(client->rpc_client, 0);
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

	response = spdk_jsonrpc_client_get_response(client->rpc_client);
	assert(response);

	sto_client_group_response(group, response);

	spdk_jsonrpc_client_free_response(response);

	if (sto_client_group_check_busy_list(group, client)) {
		return;
	}

	TAILQ_REMOVE(&group->clients, client, list);
	TAILQ_INSERT_HEAD(&group->free_clients, client, list);
}

static int
sto_client_group_poll(void *ctx)
{
	struct sto_client_group *group = ctx;
	struct sto_client *client, *tmp;

	if (TAILQ_EMPTY(&group->clients)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_FOREACH_SAFE(client, &group->clients, list, tmp) {
		sto_client_group_check_response(group, client);
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

static struct sto_rpc_cmd *
sto_rpc_cmd_alloc(struct sto_client_group *group,
		  const char *method_name, void *params,
		  sto_client_dump_params_t dump_params)
{
	struct sto_rpc_cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for a RPC cmd\n");
		return NULL;
	}

	cmd->id = group->cmd_id = sto_client_group_next_cmd_id(group);

	sto_hash_elem_init(&cmd->he, &cmd->id, sizeof(cmd->id), cmd);

	cmd->request = __alloc_jsonrpc_client_request(method_name, cmd->id, params, dump_params);
	if (spdk_unlikely(!cmd->request)) {
		SPDK_ERRLOG("Failed to create jsonrpc client request\n");
		goto free_cmd;
	}

	return cmd;

free_cmd:
	free(cmd);

	return NULL;
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
	free(cmd);
}

static void
sto_client_group_submit_cmd(struct sto_client_group *group, struct sto_client *client, struct sto_rpc_cmd *cmd)
{
	sto_hash_add_elem(group->cmd_map, &cmd->he);

	spdk_jsonrpc_client_send_request(client->rpc_client, cmd->request);
	cmd->request = NULL;
}

static void
sto_rpc_cmd_run(struct sto_client_group *group, struct sto_rpc_cmd *cmd)
{
	struct sto_client *client;

	client = TAILQ_FIRST(&group->free_clients);
	if (!client) {
		TAILQ_INSERT_TAIL(&group->cmd_busy_list, cmd, list);
		return;
	}

	sto_client_group_submit_cmd(group, client, cmd);

	TAILQ_REMOVE(&group->free_clients, client, list);
	TAILQ_INSERT_TAIL(&group->clients, client, list);
}

int
sto_client_send(const char *method_name, void *params,
		sto_client_dump_params_t dump_params,
		struct sto_client_args *args)
{
	struct sto_client_group *group;
	struct sto_rpc_cmd *cmd;

	if (spdk_unlikely(!method_name)) {
		SPDK_ERRLOG("Method name is not set\n");
		return -EINVAL;
	}

	group = g_sto_client_group;

	cmd = sto_rpc_cmd_alloc(group, method_name, params, dump_params);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to alloc `%s` RPC cmd\n", method_name);
		return -ENOMEM;
	}

	sto_rpc_cmd_init_cb(cmd, args->response_handler, args->priv);

	sto_rpc_cmd_run(group, cmd);

	return 0;
}

static void sto_client_group_close(struct sto_client_group *group);

static int
sto_client_group_connect(struct sto_client_group *group)
{
	int i;

	TAILQ_INIT(&group->free_clients);
	TAILQ_INIT(&group->clients);

	for (i = 0; i < STO_CLIENT_MAX_CONNS; i++) {
		struct sto_client *client = &group->clients_array[i];

		client->rpc_client = spdk_jsonrpc_client_connect(group->addr, group->addr_family);
		if (spdk_unlikely(!client->rpc_client)) {
			SPDK_ERRLOG("spdk_jsonrpc_client_connect() failed: %s\n", spdk_strerror(errno));
			sto_client_group_close(group);
			return -errno;
		}

		TAILQ_INSERT_TAIL(&group->free_clients, &group->clients_array[i], list);
	}

	SPDK_NOTICELOG("STO client group connect: addr[%s] family[%d]\n",
		       group->addr, group->addr_family);

	return 0;
}

static void
sto_client_group_close(struct sto_client_group *group)
{
	int i;

	for (i = 0; i < STO_CLIENT_MAX_CONNS; i++) {
		struct sto_client *client = &group->clients_array[i];

		if (spdk_unlikely(!client->rpc_client)) {
			break;
		}

		spdk_jsonrpc_client_close(client->rpc_client);
	}
}

static struct sto_client_group *
sto_client_group_alloc(const char *addr, int addr_family)
{
	struct sto_client_group *group;
	int rc;

	group = calloc(1, sizeof(*group));
	if (spdk_unlikely(!group)) {
		SPDK_ERRLOG("Failed to alloc group: addr[%s] family[%d]\n",
			    addr, addr_family);
		return ERR_PTR(-ENOMEM);
	}

	group->addr = strdup(addr);
	if (spdk_unlikely(!group->addr)) {
		SPDK_ERRLOG("Cannot allocate memory for addr %s\n", addr);
		return ERR_PTR(-ENOMEM);
	}

	group->addr_family = addr_family;

	group->cmd_map = sto_hash_alloc(STO_CLIENT_MAX_CONNS);
	if (spdk_unlikely(!group->cmd_map)) {
		SPDK_ERRLOG("Failed to create cmd map\n");
		rc = -ENOMEM;
		goto free_addr;
	}

	TAILQ_INIT(&group->cmd_busy_list);

	rc = sto_client_group_connect(group);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to connect client group: rc=%d\n", rc);
		goto free_cmd_map;
	}

	group->poller = SPDK_POLLER_REGISTER(sto_client_group_poll, group, STO_CLIENT_POLL_PERIOD);
	if (spdk_unlikely(!group->poller)) {
		SPDK_ERRLOG("Cann't register the STO client poller\n");
		rc = -ENOMEM;
		goto close_clients;
	}

	return group;

close_clients:
	sto_client_group_close(group);

free_cmd_map:
	sto_hash_free(group->cmd_map);

free_addr:
	free((char *) group->addr);

	return ERR_PTR(rc);
}

static void
sto_client_group_free(struct sto_client_group *group)
{
	spdk_poller_unregister(&group->poller);
	sto_client_group_close(group);
	sto_hash_free(group->cmd_map);
	free((char *) group->addr);
}

int
sto_client_connect(const char *addr, int addr_family)
{
	struct sto_client_group *group;

	if (g_sto_client_group) {
		SPDK_ERRLOG("FAILED: STO client has already been initialized\n");
		return -EINVAL;
	}

	group = sto_client_group_alloc(addr, addr_family);
	if (IS_ERR(group)) {
		SPDK_ERRLOG("Failed to alloc sto client group\n");
		return PTR_ERR(group);
	}

	g_sto_client_group = group;

	return 0;
}

void
sto_client_close(void)
{
	struct sto_client_group *group = g_sto_client_group;

	if (!group) {
		SPDK_ERRLOG("FAILED: STO client has not been initialized yet\n");
		return;
	}

	sto_client_group_free(group);
}
