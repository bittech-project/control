#ifndef _STO_RPC_AIO_H_
#define _STO_RPC_AIO_H_

#include "sto_async.h"

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

struct sto_rpc_writefile_args {
	void *cb_arg;
	sto_generic_cb cb_fn;
};

int sto_rpc_writefile(const char *filepath, int oflag, char *buf,
		      struct sto_rpc_writefile_args *args);

typedef void (*sto_rpc_readfile_complete)(void *cb_arg, char *buf, int rc);

void sto_rpc_readfile(const char *filepath, uint32_t size,
		      sto_rpc_readfile_complete cb_fn, void *cb_arg);

typedef void (*sto_rpc_readfile_buf_complete)(void *cb_arg, int rc);

void sto_rpc_readfile_buf(const char *filepath, uint32_t size, char **buf,
			  sto_rpc_readfile_buf_complete cb_fn, void *cb_arg);

struct sto_rpc_readlink_args {
	void *cb_arg;
	sto_generic_cb cb_fn;

	char **buf;
};

int sto_rpc_readlink(const char *filepath,
		     struct sto_rpc_readlink_args *args);

#endif /* _STO_RPC_AIO_H_ */
