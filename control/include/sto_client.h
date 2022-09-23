#ifndef _STO_CLIENT_H_
#define _STO_CLIENT_H_

#include <spdk/jsonrpc.h>

struct sto_rpc_request;

typedef void (*sto_dump_params_json)(struct spdk_json_write_ctx *w);
typedef void (*resp_handler)(struct sto_rpc_request *req,
			     struct spdk_jsonrpc_client_response *resp);

struct sto_rpc_request {
	void *priv;
	resp_handler resp_handler;

	const char *method_name;
	sto_dump_params_json params_json;

	int id;
	SLIST_ENTRY(sto_rpc_request) slist;
};

int sto_client_connect(const char *addr, int addr_family);
void sto_client_close(void);

struct sto_rpc_request *sto_rpc_req_alloc(const char *method_name,
		sto_dump_params_json params_json);
void sto_rpc_req_free(struct sto_rpc_request *req);
void sto_rpc_req_init_cb(struct sto_rpc_request *req, resp_handler resp_handler, void *priv);

int sto_client_send(struct sto_rpc_request *req);

#endif /* _STO_CLIENT_H_ */
