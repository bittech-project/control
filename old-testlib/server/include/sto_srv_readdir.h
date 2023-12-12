#ifndef _STO_SRV_READDIR_H_
#define _STO_SRV_READDIR_H_

struct spdk_json_val;
struct sto_srv_dirents;

typedef void (*sto_srv_readdir_done_t)(void *cb_arg, struct sto_srv_dirents *dirents, int rc);

struct sto_srv_readdir_args {
	void *cb_arg;
	sto_srv_readdir_done_t cb_fn;
};

int sto_srv_readdir(const struct spdk_json_val *params,
		    struct sto_srv_readdir_args *args);

#endif /* _STO_SRV_READDIR_H_ */
