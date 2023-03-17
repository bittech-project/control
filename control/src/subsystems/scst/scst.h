#ifndef _SCST_H_
#define _SCST_H_

#include "sto_async.h"

struct scst_device_params {
	char *handler_name;
	char *device_name;
	char *attributes;
};

void scst_device_params_deinit(void *params_ptr);

void scst_device_open(struct scst_device_params *params, sto_generic_cb cb_fn, void *cb_arg);
void scst_device_close(struct scst_device_params *params, sto_generic_cb cb_fn, void *cb_arg);

struct scst_target_params {
	char *driver_name;
	char *target_name;
};

void scst_target_params_deinit(void *params_ptr);

void scst_target_add(struct scst_target_params *params, sto_generic_cb cb_fn, void *cb_arg);
void scst_target_del(struct scst_target_params *params, sto_generic_cb cb_fn, void *cb_arg);

struct scst_ini_group_params {
	char *driver_name;
	char *target_name;
	char *ini_group_name;
};

void scst_ini_group_params_deinit(void *params_ptr);

void scst_ini_group_add(struct scst_ini_group_params *params, sto_generic_cb cb_fn, void *cb_arg);
void scst_ini_group_del(struct scst_ini_group_params *params, sto_generic_cb cb_fn, void *cb_arg);

struct scst_lun_params {
	uint32_t lun_id;
	char *driver_name;
	char *target_name;
	char *ini_group_name;
	char *device_name;
	char *attributes;
};

void scst_lun_params_deinit(void *params_ptr);

void scst_lun_add(struct scst_lun_params *params, sto_generic_cb cb_fn, void *cb_arg);
void scst_lun_del(struct scst_lun_params *params, sto_generic_cb cb_fn, void *cb_arg);

void scst_dumps_json(sto_generic_cb cb_fn, void *cb_arg, struct sto_json_ctx *json);
void scst_scan_system(sto_generic_cb cb_fn, void *cb_arg);
void scst_write_config(sto_generic_cb cb_fn, void *cb_arg);

static inline void
scst_write_config_step(struct sto_pipeline *pipe)
{
	scst_write_config(sto_pipeline_step_done, pipe);
}

void scst_restore_config(sto_generic_cb cb_fn, void *cb_arg);

void scst_init(sto_generic_cb cb_fn, void *cb_arg);
void scst_fini(sto_generic_cb cb_fn, void *cb_arg);

struct scst *scst_get_instance(void);

#endif /* _SCST_H_ */
