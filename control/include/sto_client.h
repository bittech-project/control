#ifndef _STO_CLIENT_H_
#define _STO_CLIENT_H_

#include <spdk/queue.h>
#include <spdk/jsonrpc.h>

#define STO_LOCAL_SERVER_ADDR "/var/tmp/sto_server.sock"

typedef void (*sto_client_dump_json_params_t)(void *priv, struct spdk_json_write_ctx *w);
typedef void (*sto_client_response_handler_t)(void *priv, struct spdk_jsonrpc_client_response *resp, int rc);

struct sto_rpc_cmd {
	void *priv;
	sto_client_response_handler_t response_handler;

	const char *method_name;
	sto_client_dump_json_params_t dump_json_params;

	int id;
	TAILQ_ENTRY(sto_rpc_cmd) list;
};

int sto_client_connect(const char *addr, int addr_family);
void sto_client_close(void);

int sto_client_send(const char *method_name,
		    sto_client_dump_json_params_t dump_json_params,
		    sto_client_response_handler_t response_handler,
		    void *priv);

#endif /* _STO_CLIENT_H_ */
