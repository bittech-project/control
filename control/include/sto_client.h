#ifndef _STO_CLIENT_H_
#define _STO_CLIENT_H_

#include <spdk/queue.h>
#include <spdk/jsonrpc.h>

#define STO_LOCAL_SERVER_ADDR "/var/tmp/sto_server.sock"

struct sto_rpc_request;

typedef void (*sto_dump_params_json)(struct sto_rpc_request *req,
				     struct spdk_json_write_ctx *w);
typedef void (*resp_handler)(struct sto_rpc_request *req,
			     struct spdk_jsonrpc_client_response *resp);

struct sto_rpc_request {
	void *priv;
	resp_handler resp_handler;

	const char *method_name;
	sto_dump_params_json params_json;

	int id;
	TAILQ_ENTRY(sto_rpc_request) list;
};

int sto_client_connect(const char *addr, int addr_family);
void sto_client_close(void);

struct sto_rpc_request *sto_rpc_req_alloc(const char *method_name,
		sto_dump_params_json params_json, void *priv);
void sto_rpc_req_init_cb(struct sto_rpc_request *req, resp_handler resp_handler);
void sto_rpc_req_free(struct sto_rpc_request *req);
int sto_client_submit(struct sto_rpc_request *req);

int sto_client_send(const char *method_name, sto_dump_params_json params_json,
		    resp_handler resp_handler, void *priv);

#endif /* _STO_CLIENT_H_ */
