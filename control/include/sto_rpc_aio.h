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

enum sto_rpc_readfile_type {
	STO_RPC_READFILE_TYPE_NONE,
	STO_RPC_READFILE_TYPE_BASIC,
	STO_RPC_READFILE_TYPE_WITH_BUF,
};

typedef void (*sto_rpc_readfile_complete)(void *cb_arg, char *buf, int rc);
typedef void (*sto_rpc_readfile_with_buf_complete)(void *cb_arg, int rc);

struct sto_rpc_readfile_args {
	void *cb_arg;

	enum sto_rpc_readfile_type type;
	union {
		struct {
			sto_rpc_readfile_complete cb_fn;
		} basic;

		struct {
			sto_rpc_readfile_with_buf_complete cb_fn;
			char **buf;
		} with_buf;
	} u;
};

#define STO_RPC_READFILE_ARGS_BASIC(_cb_arg, _cb_fn)	\
	{						\
		.cb_arg = _cb_arg,			\
		.type = STO_RPC_READFILE_TYPE_BASIC,	\
		.u.basic = {				\
			.cb_fn = _cb_fn,		\
		},					\
	}

static inline void
sto_rpc_readfile_args_basic_init(struct sto_rpc_readfile_args *args,
				 void *cb_arg, sto_rpc_readfile_complete cb_fn)
{
	args->cb_arg = cb_arg;
	args->type = STO_RPC_READFILE_TYPE_BASIC;
	args->u.basic.cb_fn = cb_fn;
}

#define STO_RPC_READFILE_ARGS_WITH_BUF(_cb_arg, _cb_fn, _buf)	\
	{							\
		.cb_arg = _cb_arg,				\
		.type = STO_RPC_READFILE_TYPE_WITH_BUF,		\
		.u.with_buf = {					\
			.cb_fn = _cb_fn,			\
			.buf = &_buf,				\
		},						\
	}

static inline void
sto_rpc_readfile_args_with_buf_init(struct sto_rpc_readfile_args *args,
				    void *cb_arg, sto_rpc_readfile_with_buf_complete cb_fn,
				    char *buf)
{
	args->cb_arg = cb_arg;
	args->type = STO_RPC_READFILE_TYPE_WITH_BUF;
	args->u.with_buf.cb_fn = cb_fn;
	args->u.with_buf.buf = &buf;
}

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
