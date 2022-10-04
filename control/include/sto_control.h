#ifndef _STO_CONTROL_H_
#define _STO_CONTROL_H_

typedef void (*spdk_control_init_cb)(void *cb_arg, int rc);
void spdk_control_init(spdk_control_init_cb cb_fn, void *cb_arg);

typedef void (*spdk_control_fini_cb)(void *arg);
void spdk_control_fini(spdk_control_fini_cb cb_fn, void *cb_arg);

void spdk_control_config_json(struct spdk_json_write_ctx *w);

#endif /* _STO_CONTROL_H_ */
