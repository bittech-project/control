#ifndef _STO_SRV_SUBPROCESS_H_
#define _STO_SRV_SUBPROCESS_H_

struct spdk_json_val;

typedef void (*sto_srv_subprocess_done_t)(void *cb_arg, char *output, int rc);

struct sto_srv_subprocess_args {
	void *cb_arg;
	sto_srv_subprocess_done_t cb_fn;
};

int sto_srv_subprocess(const struct spdk_json_val *params,
		       struct sto_srv_subprocess_args *args);

#endif /* _STO_SRV_SUBPROCESS_H_ */
