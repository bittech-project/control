#ifndef _STO_RPC_AIO_H_
#define _STO_RPC_AIO_H_

#include "sto_async.h"

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

void sto_rpc_writefile(const char *filepath, int oflag, char *buf,
		       sto_generic_cb cb_fn, void *cb_arg);

typedef void (*sto_rpc_readfile_complete)(void *cb_arg, char *buf, int rc);

void sto_rpc_readfile(const char *filepath, uint32_t size,
		      sto_rpc_readfile_complete cb_fn, void *cb_arg);

typedef void (*sto_rpc_readfile_buf_complete)(void *cb_arg, int rc);

void sto_rpc_readfile_buf(const char *filepath, uint32_t size,
			  sto_rpc_readfile_buf_complete cb_fn, void *cb_arg,
			  char **buf);

void sto_rpc_readlink(const char *filepath, sto_generic_cb cb_fn, void *cb_arg, char **buf);

#endif /* _STO_RPC_AIO_H_ */
