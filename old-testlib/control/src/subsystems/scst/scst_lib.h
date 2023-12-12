#ifndef _SCST_LIB_H_
#define _SCST_LIB_H_

#include "sto_async.h"
#include "sto_json.h"
#include "sto_pipeline.h"
#include "sto_hash.h"

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

struct scst;
struct scst_device_handler;
struct scst_target_driver;

struct scst_device {
	const char *name;

	struct scst_device_handler *handler;

	struct sto_hash_elem he;
	TAILQ_ENTRY(scst_device) list;
};

struct scst_device_handler {
	const char *name;

	struct scst *scst;

	TAILQ_HEAD(, scst_device) device_list;
	int ref_cnt;

	TAILQ_ENTRY(scst_device_handler) list;
};

struct scst_lun {
	uint32_t id;

	struct scst_device *device;

	TAILQ_ENTRY(scst_lun) list;
};

TAILQ_HEAD(scst_lun_list, scst_lun);

struct scst_ini_group {
	const char *name;

	struct scst_target *target;

	struct scst_lun_list lun_list;

	TAILQ_ENTRY(scst_ini_group) list;
};

struct scst_target {
	const char *name;

	struct scst_target_driver *driver;

	struct scst_lun_list lun_list;
	TAILQ_HEAD(, scst_ini_group) group_list;

	TAILQ_ENTRY(scst_target) list;
};

struct scst_target_driver {
	const char *name;

	struct scst *scst;

	TAILQ_HEAD(, scst_target) target_list;
	int ref_cnt;

	TAILQ_ENTRY(scst_target_driver) list;
};

struct scst {
	const char *config_path;

	struct sto_pipeline_engine *engine;

	TAILQ_HEAD(, scst_device_handler) handler_list;
	TAILQ_HEAD(, scst_target_driver) driver_list;

	struct sto_hash device_lookup_map;
};

struct scst_device_handler *scst_device_handler_next(struct scst *scst, struct scst_device_handler *handler);

static inline const char *
scst_device_handler_mgmt_path(const char *handler_name)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
				  handler_name, SCST_MGMT_IO);
}

struct scst_device *scst_device_next(struct scst_device_handler *handler, struct scst_device *device);

static inline const char *
scst_device_path(struct scst_device *device)
{
	struct scst_device_handler *handler = device->handler;

	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
				  handler->name, device->name);
}

struct scst_target_driver *scst_target_driver_next(struct scst *scst, struct scst_target_driver *driver);

static inline const char *
scst_target_driver_mgmt_path(const char *driver_name)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  driver_name, SCST_MGMT_IO);
}

struct scst_target *scst_target_next(struct scst_target_driver *driver, struct scst_target *target);

struct scst_ini_group *scst_ini_group_next(struct scst_target *target, struct scst_ini_group *ini_group);

static inline const char *
scst_ini_group_mgmt_path(const char *driver_name, const char *target_name)
{
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  driver_name, target_name, "ini_groups", SCST_MGMT_IO);
}

static inline const char *
scst_target_lun_mgmt_path(const char *driver_name, const char *target_name, const char *ini_group_name)
{
	if (ini_group_name) {
		return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s/%s/%s", SCST_ROOT,
					  SCST_TARGETS, driver_name,
					  target_name, "ini_groups", ini_group_name,
					  "luns", SCST_MGMT_IO);
	} else {
		return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT,
					  SCST_TARGETS, driver_name,
					  target_name, "luns", SCST_MGMT_IO);
	}
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

struct scst *scst_create(void);
void scst_destroy(struct scst *scst);

void scst_pipeline(struct scst *scst, const struct sto_pipeline_properties *properties,
		   sto_generic_cb cb_fn, void *cb_arg, void *priv);

void scst_read_available_attrs(const char *mgmt_path, const char *prefix,
			       sto_generic_cb cb_fn, void *cb_arg, char ***available_attrs);
void scst_available_attrs_print(char **available_attrs);
bool scst_available_attrs_find(char **available_attrs, char *attr);
void scst_available_attrs_destroy(char **available_attrs);

void scst_serialize_attrs(struct sto_tree_node *obj_node, struct spdk_json_write_ctx *w);

typedef void (*scst_read_attrs_done_t)(void *cb_arg, struct sto_json_ctx *json, int rc);
void scst_read_attrs(const char *dirpath, scst_read_attrs_done_t cb_fn, void *cb_arg);

struct scst_device *scst_find_device(struct scst *scst, const char *device_name);
int scst_add_device(struct scst *scst, const char *handler_name, const char *device_name);
int scst_remove_device(struct scst *scst, const char *device_name);

struct scst_target *scst_find_target(struct scst *scst, const char *driver_name, const char *target_name);
int scst_add_target(struct scst *scst, const char *driver_name, const char *target_name);
int scst_remove_target(struct scst *scst, const char *driver_name, const char *target_name);

struct scst_ini_group *scst_find_ini_group(struct scst *scst, const char *driver_name,
					   const char *target_name, const char *ini_group_name);
int scst_add_ini_group(struct scst *scst, const char *driver_name,
		       const char *target_name, const char *ini_group_name);
int scst_remove_ini_group(struct scst *scst, const char *driver_name,
			  const char *target_name, const char *ini_group_name);

struct scst_lun *scst_find_lun(struct scst *scst, const char *driver_name,
			       const char *target_name, const char *ini_group_name,
			       uint32_t lun_id);
int scst_add_lun(struct scst *scst, const char *driver_name,
		 const char *target_name, const char *ini_group_name,
		 const char *device_name, uint32_t lun_id);
int scst_remove_lun(struct scst *scst, const char *driver_name,
		    const char *target_name, const char *ini_group_name,
		    uint32_t lun_id);

#endif /* _SCST_LIB_H_ */
