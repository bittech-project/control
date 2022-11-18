#ifndef _STO_RPC_AIO_H_
#define _STO_RPC_AIO_H_

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

typedef void (*sto_rpc_writefile_done_t)(void *priv, int rc);

struct sto_rpc_writefile_args {
	void *priv;
	sto_rpc_writefile_done_t done;
};

int sto_rpc_writefile(const char *filepath, char *buf,
		      struct sto_rpc_writefile_args *args);

typedef void (*sto_rpc_readfile_done_t)(void *priv, int rc);

struct sto_rpc_readfile_args {
	void *priv;
	sto_rpc_readfile_done_t done;

	char **buf;
};

int sto_rpc_readfile(const char *filepath, uint32_t size,
		     struct sto_rpc_readfile_args *args);

#endif /* _STO_RPC_AIO_H_ */
