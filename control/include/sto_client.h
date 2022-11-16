#ifndef _STO_CLIENT_H_
#define _STO_CLIENT_H_

#include <spdk/queue.h>
#include <spdk/jsonrpc.h>

#define STO_LOCAL_SERVER_ADDR "/var/tmp/sto_server.sock"

struct sto_rpc_request;
typedef void (*sto_dump_params_json)(struct sto_rpc_request *req,
				     struct spdk_json_write_ctx *w);

typedef void (*response_handler_t)(void *priv, struct spdk_jsonrpc_client_response *resp, int rc);

struct sto_rpc_request {
	void *priv;
	response_handler_t response_handler;

	const char *method_name;
	sto_dump_params_json params_json;

	int id;
	TAILQ_ENTRY(sto_rpc_request) list;
};

int sto_client_connect(const char *addr, int addr_family);
void sto_client_close(void);

int sto_client_send(const char *method_name, sto_dump_params_json params_json,
		    response_handler_t response_handler, void *priv);

#endif /* _STO_CLIENT_H_ */
