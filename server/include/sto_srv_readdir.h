#ifndef _STO_SRV_READDIR_H_
#define _STO_SRV_READDIR_H_

struct spdk_json_val;
struct sto_srv_dirents;

typedef void (*sto_srv_readdir_done_t)(void *priv, struct sto_srv_dirents *dirents, int rc);

struct sto_srv_readdir_args {
	void *priv;
	sto_srv_readdir_done_t done;
};

int sto_srv_readdir(const struct spdk_json_val *params,
		    struct sto_srv_readdir_args *args);

#endif /* _STO_SRV_READDIR_H_ */
