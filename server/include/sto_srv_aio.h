#ifndef _STO_SRV_AIO_H_
#define _STO_SRV_AIO_H_

struct spdk_json_val;
typedef void (*sto_srv_writefile_done_t)(void *priv, int rc);

struct sto_srv_writefile_args {
	void *priv;
	sto_srv_writefile_done_t done;
};

int sto_srv_writefile(const struct spdk_json_val *params,
		      struct sto_srv_writefile_args *args);

typedef void (*sto_srv_readfile_done_t)(void *priv, char *buf, int rc);

struct sto_srv_readfile_args {
	void *priv;
	sto_srv_readfile_done_t done;
};

int sto_srv_readfile(const struct spdk_json_val *params,
		     struct sto_srv_readfile_args *args);

#endif /* _STO_SRV_AIO_H_ */
