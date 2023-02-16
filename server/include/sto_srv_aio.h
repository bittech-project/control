#ifndef _STO_SRV_AIO_H_
#define _STO_SRV_AIO_H_

#include "sto_async.h"

struct spdk_json_val;

struct sto_srv_writefile_args {
	void *cb_arg;
	sto_generic_cb cb_fn;
};

int sto_srv_writefile(const struct spdk_json_val *params,
		      struct sto_srv_writefile_args *args);

typedef void (*sto_srv_readfile_done_t)(void *cb_arg, char *buf, int rc);

struct sto_srv_readfile_args {
	void *cb_arg;
	sto_srv_readfile_done_t cb_fn;
};

int sto_srv_readfile(const struct spdk_json_val *params,
		     struct sto_srv_readfile_args *args);

typedef void (*sto_srv_readlink_done_t)(void *cb_arg, char *buf, int rc);

struct sto_srv_readlink_args {
	void *cb_arg;
	sto_srv_readlink_done_t cb_fn;
};

int sto_srv_readlink(const struct spdk_json_val *params,
		     struct sto_srv_readlink_args *args);

#endif /* _STO_SRV_AIO_H_ */
