#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

typedef void (*spdk_sto_init_cb)(void *cb_arg, int rc);
void spdk_sto_init(spdk_sto_init_cb cb_fn, void *cb_arg);

typedef void (*spdk_sto_fini_cb)(void *arg);
void spdk_sto_fini(spdk_sto_fini_cb cb_fn, void *cb_arg);

void spdk_sto_config_json(struct spdk_json_write_ctx *w);

#endif /* _STO_SUBSYSTEM_H_ */
