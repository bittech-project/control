#ifndef _SCST_H_
#define _SCST_H_

#include "sto_async.h"

struct scst_device_params {
	char *handler_name;
	char *device_name;
	char *attributes;
};

static inline void
scst_device_params_deinit(void *params_ptr)
{
	struct scst_device_params *params = params_ptr;

	free(params->handler_name);
	free(params->device_name);
	free(params->attributes);
}

void scst_device_open(struct scst_device_params *params, sto_generic_cb cb_fn, void *cb_arg);
void scst_device_close(struct scst_device_params *params, sto_generic_cb cb_fn, void *cb_arg);

struct scst_ini_group_params {
	char *driver_name;
	char *target_name;
	char *ini_group_name;
};

static inline void
scst_ini_group_params_deinit(void *params_ptr)
{
	struct scst_ini_group_params *params = params_ptr;

	free(params->driver_name);
	params->driver_name = NULL;

	free(params->target_name);
	params->target_name = NULL;

	free(params->ini_group_name);
	params->ini_group_name = NULL;
}

void scst_ini_group_add(struct scst_ini_group_params *params, sto_generic_cb cb_fn, void *cb_arg);
void scst_ini_group_del(struct scst_ini_group_params *params, sto_generic_cb cb_fn, void *cb_arg);

struct scst_target_params {
	char *driver_name;
	char *target_name;
};

static inline void
scst_target_params_deinit(void *params_ptr)
{
	struct scst_target_params *params = params_ptr;

	free(params->driver_name);
	params->driver_name = NULL;

	free(params->target_name);
	params->target_name = NULL;
}

void scst_target_add(struct scst_target_params *params, sto_generic_cb cb_fn, void *cb_arg);
void scst_target_del(struct scst_target_params *params, sto_generic_cb cb_fn, void *cb_arg);

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
