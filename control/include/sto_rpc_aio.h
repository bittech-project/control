#ifndef _STO_RPC_AIO_H_
#define _STO_RPC_AIO_H_

#include "sto_async.h"

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

struct sto_rpc_writefile_args {
	const char *filepath;
	int oflag;
	char *buf;
};

static inline void
sto_rpc_writefile_args_deinit(struct sto_rpc_writefile_args *args)
{
	args->oflag = 0;

	free((char *) args->filepath);
	args->filepath = NULL;

	free(args->buf);
	args->buf = NULL;
}

void sto_rpc_writefile(const char *filepath, int oflag, char *buf,
		       sto_generic_cb cb_fn, void *cb_arg);

static inline void
sto_rpc_writefile_args(struct sto_rpc_writefile_args *args, sto_generic_cb cb_fn, void *cb_arg)
{
	sto_rpc_writefile(args->filepath, args->oflag, args->buf, cb_fn, cb_arg);
	sto_rpc_writefile_args_deinit(args);
}

typedef void (*sto_rpc_readfile_complete)(void *cb_arg, char *buf, int rc);

void sto_rpc_readfile(const char *filepath, uint32_t size,
		      sto_rpc_readfile_complete cb_fn, void *cb_arg);

typedef void (*sto_rpc_readfile_buf_complete)(void *cb_arg, int rc);

void sto_rpc_readfile_buf(const char *filepath, uint32_t size,
			  sto_rpc_readfile_buf_complete cb_fn, void *cb_arg,
			  char **buf);

void sto_rpc_readlink(const char *filepath, sto_generic_cb cb_fn, void *cb_arg, char **buf);

#endif /* _STO_RPC_AIO_H_ */
