#ifndef _SCST_H_
#define _SCST_H_

#include "sto_async.h"
#include "sto_json.h"
#include "sto_pipeline.h"

struct sto_tree_node;
struct sto_pipeline_properties;

#define SCST_ROOT "/sys/kernel/scst_tgt"

/* Root-level */
#define SCST_SGV	"sgv"
#define SCST_HANDLERS	"handlers"
#define SCST_DEVICES	"devices"
#define SCST_TARGETS	"targets"
#define SCST_DEV_GROUPS	"device_groups"
#define SCST_QUEUE_RES	"last_sysfs_mgmt_res"

/* Device group specific */
#define SCST_DG_DEVICES	"devices"
#define SCST_DG_TGROUPS	"target_groups"

/* Target specific */
#define SCST_GROUPS	"ini_groups"
#define SCST_INITIATORS	"initiators"
#define SCST_SESSIONS	"sessions"
#define SCST_LUNS	"luns"

/* Files */
#define SCST_MGMT_IO	"mgmt"
#define SCST_VERSION_IO	"version"
#define SCST_TRACE_IO	"trace_level"
#define SCST_RESYNC_IO	"resync_size"
#define SCST_T10_IO	"t10_dev_id"

static inline const char *
scst_handler_mgmt(const char *handler)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
				  handler, SCST_MGMT_IO);
}

static inline const char *
scst_dev_groups_mgmt(void)
{
	return spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS, SCST_MGMT_IO);
}

static inline const char *
scst_dev_group_devices_mgmt(const char *dev_group)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  dev_group, "devices", SCST_MGMT_IO);
}

static inline const char *
scst_dev_group_target_groups_mgmt(const char *dev_group)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  dev_group, "target_groups", SCST_MGMT_IO);
}

static inline const char *
scst_dev_group_target_group_mgmt(const char *dev_group, const char *target_group)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  dev_group, "target_groups", target_group, SCST_MGMT_IO);
}

static inline const char *
scst_target_driver_mgmt(const char *target_driver)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  target_driver, SCST_MGMT_IO);
}

static inline const char *
scst_target_ini_groups_mgmt(const char *target_driver, const char *target)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  target_driver, target, "ini_groups", SCST_MGMT_IO);
}

static inline const char *
scst_target_lun_mgmt(const char *target_driver, const char *target, const char *ini_group)
{
	if (ini_group) {
		return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s/%s/%s", SCST_ROOT,
					  SCST_TARGETS, target_driver,
					  target, "ini_groups", ini_group,
					  "luns", SCST_MGMT_IO);
	}

	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT,
				  SCST_TARGETS, target_driver,
				  target, "luns", SCST_MGMT_IO);
}

void scst_read_available_attrs(const char *mgmt_path, const char *prefix,
			       sto_generic_cb cb_fn, void *cb_arg, char ***available_attrs);
void scst_available_attrs_print(char **available_attrs);
bool scst_available_attrs_find(char **available_attrs, char *attr);
void scst_available_attrs_destroy(char **available_attrs);

void scst_serialize_attrs(struct sto_tree_node *obj_node, struct spdk_json_write_ctx *w);

typedef void (*scst_read_attrs_done_t)(void *cb_arg, struct sto_json_ctx *json, int rc);
void scst_read_attrs(const char *dirpath, scst_read_attrs_done_t cb_fn, void *cb_arg);

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

void scst_dumps_json(sto_generic_cb cb_fn, void *cb_arg, struct sto_json_ctx *json);
void scst_scan_system(sto_generic_cb cb_fn, void *cb_arg);
void scst_write_config(sto_generic_cb cb_fn, void *cb_arg);

static inline void
scst_write_config_step(struct sto_pipeline *pipe)
{
	scst_write_config(sto_pipeline_step_done, pipe);
}

void scst_pipeline(const struct sto_pipeline_properties *properties,
		   sto_generic_cb cb_fn, void *cb_arg, void *priv);

void scst_init(sto_generic_cb cb_fn, void *cb_arg);
void scst_fini(sto_generic_cb cb_fn, void *cb_arg);

#endif /* _SCST_H_ */
