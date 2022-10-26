#ifndef _STO_SERVER_H_
#define _STO_SERVER_H_

#include <spdk/jsonrpc.h>

#define STO_LOCAL_SERVER_ADDR "/var/tmp/sto_server.sock"

typedef void (*sto_rpc_method_handler)(struct spdk_jsonrpc_request *request,
				       const struct spdk_json_val *params);

int sto_server_start(void);
void sto_server_fini(void);

void sto_rpc_register_method(const char *method, sto_rpc_method_handler func);

#define STO_RPC_REGISTER(method, func)					\
static void __attribute__((constructor(1000))) rpc_register_##func(void)\
{									\
	sto_rpc_register_method(method, func);				\
}

#endif /* _STO_SERVER_H_ */
