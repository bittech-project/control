#ifndef _STO_RPC_AIO_H_
#define _STO_RPC_AIO_H_

#include "sto_async.h"

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

struct sto_rpc_writefile_args {
	void *priv;
	sto_async_done_t done;
};

int sto_rpc_writefile(const char *filepath, int oflag, char *buf,
		      struct sto_rpc_writefile_args *args);

struct sto_rpc_readfile_args {
	void *priv;
	sto_async_done_t done;

	char **buf;
};

int sto_rpc_readfile(const char *filepath, uint32_t size,
		     struct sto_rpc_readfile_args *args);

struct sto_rpc_readlink_args {
	void *priv;
	sto_async_done_t done;

	char **buf;
};

int sto_rpc_readlink(const char *filepath,
		     struct sto_rpc_readlink_args *args);

#endif /* _STO_RPC_AIO_H_ */
