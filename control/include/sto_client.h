#ifndef _STO_CLIENT_H_
#define _STO_CLIENT_H_

struct spdk_json_write_ctx;
struct spdk_jsonrpc_client_response;

#define STO_LOCAL_SERVER_ADDR "/var/tmp/sto_server.sock"

typedef void (*sto_client_dump_params_t)(void *priv, struct spdk_json_write_ctx *w);
typedef void (*sto_client_response_handler_t)(void *priv,
					      struct spdk_jsonrpc_client_response *resp,
					      int rc);

struct sto_client_args {
	void *priv;
	sto_client_response_handler_t response_handler;
};

int sto_client_connect(const char *addr, int addr_family);
void sto_client_close(void);

int sto_client_send(const char *method_name,
		    void *params, sto_client_dump_params_t dump_params,
		    struct sto_client_args *args);

#endif /* _STO_CLIENT_H_ */
