#ifndef _STO_RPC_H_
#define _STO_RPC_H_

#include <spdk/jsonrpc.h>

typedef void (*sto_rpc_method_handler)(struct spdk_jsonrpc_request *request,
				       const struct spdk_json_val *params);

void sto_rpc_register_method(const char *method, sto_rpc_method_handler func);

#define STO_RPC_REGISTER(method, func)					\
static void __attribute__((constructor(1000))) rpc_register_##func(void)\
{									\
	sto_rpc_register_method(method, func);				\
}

#endif /* _STO_RPC_H_ */
