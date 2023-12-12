#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "scst_lib.h"
#include "scst.h"

#include "sto_generic_req.h"
#include "sto_subsystem.h"
#include "sto_inode.h"
#include "sto_lib.h"
#include "sto_tree.h"

struct spdk_json_write_ctx;

static int
scst_parse_attributes(char *attributes, char **data)
{
	char *parsed_attributes, *c, *tmp;
	int rc = 0;

	parsed_attributes = strdup(attributes);
	if (spdk_unlikely(!parsed_attributes)) {
		SPDK_ERRLOG("Failed to alloc memory for parsed attributes\n");
		return -ENOMEM;
	}

	for (c = parsed_attributes; *c != '\0'; c++) {
		if (*c == ',') {
			*c = ';';
		}
	}

	tmp = spdk_sprintf_append_realloc(*data, " %s", parsed_attributes);
	if (spdk_unlikely(!tmp)) {
		SPDK_ERRLOG("Failed to realloc memory for attributes data\n");
		rc = -ENOMEM;
		goto out;
	}

	*data = tmp;

out:
	free(parsed_attributes);

	return rc;
}

struct snapshot_req_params {
	char *dirpath;
};

static void
snapshot_req_params_deinit(void *params_ptr)
{
	struct snapshot_req_params *params = params_ptr;

	free(params->dirpath);
}

struct snapshot_req_priv {
	struct sto_json_ctx json;
};

static void
snapshot_req_priv_deinit(void *priv_ptr)
{
	struct snapshot_req_priv *priv = priv_ptr;

	sto_json_ctx_destroy(&priv->json);
}

static int
snapshot_req_constructor(void *arg1, const void *arg2)
{
	struct snapshot_req_params *req_params = arg1;

	req_params->dirpath = spdk_sprintf_alloc("%s", SCST_ROOT);
	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	return 0;
}

static void
snapshot_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct snapshot_req_priv *priv = sto_req_get_priv(req);

	scst_dumps_json(sto_pipeline_step_done, pipe, &priv->json);
}

static void
snapshot_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct snapshot_req_priv *priv = sto_req_get_priv(req);

	spdk_json_write_val(w, priv->json.values);
}

const struct sto_req_properties snapshot_req_properties = {
	.params_size = sizeof(struct snapshot_req_params),
	.params_deinit_fn = snapshot_req_params_deinit,

	.priv_size = sizeof(struct snapshot_req_priv),
	.priv_deinit_fn = snapshot_req_priv_deinit,

	.response = snapshot_response,
	.steps = {
		STO_PL_STEP(snapshot_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

static int
scst_handler_list_constructor(void *arg1, const void *arg2)
{
	struct sto_readdir_req_params *req_params = arg1;

	req_params->name = spdk_sprintf_alloc("handlers");
	if (spdk_unlikely(!req_params->name)) {
		return -ENOMEM;
	}

	req_params->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_HANDLERS);
	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_driver_list_constructor(void *arg1, const void *arg2)
{
	struct sto_readdir_req_params *req_params = arg1;

	req_params->name = spdk_sprintf_alloc("drivers");
	if (spdk_unlikely(!req_params->name)) {
		return -ENOMEM;
	}

	req_params->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	return 0;
}

struct dev_open_ops_params {
	char *device;
	char *handler;
	char *attributes;
};

static const struct sto_ops_param_dsc dev_open_ops_params_descriptors[] = {
	STO_OPS_PARAM_STR(device, struct dev_open_ops_params, "SCST device name"),
	STO_OPS_PARAM_STR(handler, struct dev_open_ops_params, "SCST handler name"),
	STO_OPS_PARAM_STR_OPTIONAL(attributes, struct dev_open_ops_params, "SCST device attributes <p=v,...>"),
};

static const struct sto_ops_params_properties dev_open_ops_params_properties =
	STO_OPS_PARAMS_INITIALIZER(dev_open_ops_params_descriptors, struct dev_open_ops_params);

static int
dev_open_req_constructor(void *arg1, const void *arg2)
{
	struct scst_device_params *req_params = arg1;
	struct dev_open_ops_params *ops_params = (void *) arg2;

	req_params->device_name = ops_params->device;
	ops_params->device = NULL;

	req_params->handler_name = ops_params->handler;
	ops_params->handler = NULL;

	if (ops_params->attributes) {
		return scst_parse_attributes(ops_params->attributes, &req_params->attributes);
	}

	return 0;
}

static void
dev_open_req_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct scst_device_params *params = sto_req_get_params(req);

	scst_device_open(params, sto_pipeline_step_done, pipe);
}

static void dev_close_req_step(struct sto_pipeline *pipe);

const struct sto_req_properties dev_open_req_properties = {
	.params_size = sizeof(struct scst_device_params),
	.params_deinit_fn = scst_device_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(dev_open_req_step, dev_close_req_step),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

struct dev_close_ops_params {
	char *device;
	char *handler;
};

static const struct sto_ops_param_dsc dev_close_ops_params_descriptors[] = {
	STO_OPS_PARAM_STR(device, struct dev_close_ops_params, "SCST device name"),
	STO_OPS_PARAM_STR(handler, struct dev_close_ops_params, "SCST handler name"),
};

static const struct sto_ops_params_properties dev_close_ops_params_properties =
	STO_OPS_PARAMS_INITIALIZER(dev_close_ops_params_descriptors, struct dev_close_ops_params);

static int
dev_close_req_constructor(void *arg1, const void *arg2)
{
	struct scst_device_params *req_params = arg1;
	struct dev_close_ops_params *ops_params = (void *) arg2;

	req_params->device_name = ops_params->device;
	ops_params->device = NULL;

	req_params->handler_name = ops_params->handler;
	ops_params->handler = NULL;

	return 0;
}

static void
dev_close_req_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct scst_device_params *params = sto_req_get_params(req);

	scst_device_close(params, sto_pipeline_step_done, pipe);
}

const struct sto_req_properties dev_close_req_properties = {
	.params_size = sizeof(struct scst_device_params),
	.params_deinit_fn = scst_device_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(dev_close_req_step, NULL),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

struct scst_dev_resync_params {
	char *device;
};

static const struct sto_ops_param_dsc scst_dev_resync_params_descriptors[] = {
	STO_OPS_PARAM_STR(device, struct scst_dev_resync_params, "SCST device name"),
};

static const struct sto_ops_params_properties scst_dev_resync_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_dev_resync_params_descriptors, struct scst_dev_resync_params);

static int
scst_dev_resync_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_dev_resync_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEVICES,
					      ops_params->device, "resync_size");
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("1");
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_dev_list_constructor(void *arg1, const void *arg2)
{
	struct sto_readdir_req_params *req_params = arg1;

	req_params->name = spdk_sprintf_alloc("devices");
	if (spdk_unlikely(!req_params->name)) {
		return -ENOMEM;
	}

	req_params->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEVICES);
	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	return 0;
}

struct scst_dgrp_params {
	char *dgrp;
};

static const struct sto_ops_param_dsc scst_dgrp_params_descriptors[] = {
	STO_OPS_PARAM_STR(dgrp, struct scst_dgrp_params, "SCST device group name"),
};

static const struct sto_ops_params_properties scst_dgrp_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_dgrp_params_descriptors, struct scst_dgrp_params);

static int
scst_dgrp_add_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_dgrp_params *ops_params = arg2;

	req_params->file = scst_dev_groups_mgmt();
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("create %s", ops_params->dgrp);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_dgrp_del_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_dgrp_params *ops_params = arg2;

	req_params->file = scst_dev_groups_mgmt();
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->dgrp);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_dgrp_list_constructor(void *arg1, const void *arg2)
{
	struct sto_readdir_req_params *req_params = arg1;

	req_params->name = spdk_sprintf_alloc("Device Group");
	if (spdk_unlikely(!req_params->name)) {
		return -ENOMEM;
	}

	req_params->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEV_GROUPS);
	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	req_params->exclude_list[0] = SCST_MGMT_IO;

	return 0;
}

struct scst_dgrp_dev_params {
	char *dgrp;
	char *device;
};

static const struct sto_ops_param_dsc scst_dgrp_dev_params_descriptors[] = {
	STO_OPS_PARAM_STR(dgrp, struct scst_dgrp_dev_params, "SCST device group name"),
	STO_OPS_PARAM_STR(device, struct scst_dgrp_dev_params, "SCST device name"),
};

static const struct sto_ops_params_properties scst_dgrp_dev_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_dgrp_dev_params_descriptors, struct scst_dgrp_dev_params);

static int
scst_dgrp_add_dev_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_dgrp_dev_params *ops_params = arg2;

	req_params->file = scst_dev_group_devices_mgmt(ops_params->dgrp);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("add %s", ops_params->device);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_dgrp_del_dev_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_dgrp_dev_params *ops_params = arg2;

	req_params->file = scst_dev_group_devices_mgmt(ops_params->dgrp);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->device);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct scst_tgrp_params {
	char *tgrp;
	char *dgrp;
};

static const struct sto_ops_param_dsc scst_tgrp_params_descriptors[] = {
	STO_OPS_PARAM_STR(tgrp, struct scst_tgrp_params, "SCST target group name"),
	STO_OPS_PARAM_STR(dgrp, struct scst_tgrp_params, "SCST device group name"),
};

static const struct sto_ops_params_properties scst_tgrp_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_tgrp_params_descriptors, struct scst_tgrp_params);

static int
scst_tgrp_add_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_tgrp_params *ops_params = arg2;

	req_params->file = scst_dev_group_target_groups_mgmt(ops_params->dgrp);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("add %s", ops_params->tgrp);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_tgrp_del_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_tgrp_params *ops_params = arg2;

	req_params->file = scst_dev_group_target_groups_mgmt(ops_params->dgrp);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->tgrp);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct scst_tgrp_list_params {
	char *dgrp;
};

static const struct sto_ops_param_dsc scst_tgrp_list_params_descriptors[] = {
	STO_OPS_PARAM_STR(dgrp, struct scst_tgrp_list_params, "SCST device group name"),
};

static const struct sto_ops_params_properties scst_tgrp_list_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_tgrp_list_params_descriptors, struct scst_tgrp_list_params);

static int
scst_tgrp_list_constructor(void *arg1, const void *arg2)
{
	struct sto_readdir_req_params *req_params = arg1;
	const struct scst_tgrp_list_params *ops_params = arg2;

	req_params->name = spdk_sprintf_alloc("Target Groups");
	if (spdk_unlikely(!req_params->name)) {
		return -ENOMEM;
	}

	req_params->dirpath = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
						 ops_params->dgrp, "target_groups");
	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	req_params->exclude_list[0] = SCST_MGMT_IO;

	return 0;
}

struct scst_tgrp_tgt_params {
	char *target;
	char *dgrp;
	char *tgrp;
};

static const struct sto_ops_param_dsc scst_tgrp_tgt_params_descriptors[] = {
	STO_OPS_PARAM_STR(target, struct scst_tgrp_tgt_params, "SCST target name"),
	STO_OPS_PARAM_STR(dgrp, struct scst_tgrp_tgt_params, "SCST device group name"),
	STO_OPS_PARAM_STR(tgrp, struct scst_tgrp_tgt_params, "SCST target group name"),
};

static const struct sto_ops_params_properties scst_tgrp_tgt_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_tgrp_tgt_params_descriptors, struct scst_tgrp_tgt_params);

static int
scst_tgrp_add_tgt_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_tgrp_tgt_params *ops_params = arg2;

	req_params->file = scst_dev_group_target_group_mgmt(ops_params->dgrp,
							    ops_params->tgrp);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("add %s", ops_params->target);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_tgrp_del_tgt_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_tgrp_tgt_params *ops_params = arg2;

	req_params->file = scst_dev_group_target_group_mgmt(ops_params->dgrp,
							    ops_params->tgrp);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->target);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct target_ops_params {
	char *target;
	char *driver;
};

static const struct sto_ops_param_dsc target_ops_params_descriptors[] = {
	STO_OPS_PARAM_STR(target, struct target_ops_params, "SCST target name"),
	STO_OPS_PARAM_STR(driver, struct target_ops_params, "SCST target driver name"),
};

static const struct sto_ops_params_properties target_ops_params_properties =
	STO_OPS_PARAMS_INITIALIZER(target_ops_params_descriptors, struct target_ops_params);

static int
target_add_req_constructor(void *arg1, const void *arg2)
{
	struct scst_target_params *req_params = arg1;
	struct target_ops_params *ops_params = (void *) arg2;

	req_params->driver_name = ops_params->driver;
	ops_params->driver = NULL;

	req_params->target_name = ops_params->target;
	ops_params->target = NULL;

	return 0;
}

static void
target_add_req_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct scst_target_params *params = sto_req_get_params(req);

	scst_target_add(params, sto_pipeline_step_done, pipe);
}

static void target_del_req_step(struct sto_pipeline *pipe);

const struct sto_req_properties target_add_req_properties = {
	.params_size = sizeof(struct scst_target_params),
	.params_deinit_fn = scst_target_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(target_add_req_step, target_del_req_step),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

static int
target_del_req_constructor(void *arg1, const void *arg2)
{
	return target_add_req_constructor(arg1, arg2);
}

static void
target_del_req_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct scst_target_params *params = sto_req_get_params(req);

	scst_target_del(params, sto_pipeline_step_done, pipe);
}

const struct sto_req_properties target_del_req_properties = {
	.params_size = sizeof(struct scst_target_params),
	.params_deinit_fn = scst_target_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(target_del_req_step, NULL),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

struct scst_target_list_params {
	char *driver;
};

static const struct sto_ops_param_dsc scst_target_list_params_descriptors[] = {
	STO_OPS_PARAM_STR(driver, struct scst_target_list_params, "SCST target driver name"),
};

static const struct sto_ops_params_properties scst_target_list_params_properties =
	STO_OPS_PARAMS_INITIALIZER_EMPTY(scst_target_list_params_descriptors,
					 sizeof(struct scst_target_list_params));


static int
scst_target_list_constructor(void *arg1, const void *arg2)
{
	struct sto_tree_req_params *req_params = arg1;
	const struct scst_target_list_params *ops_params = arg2;

	if (ops_params) {
		req_params->dirpath = spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT,
							 SCST_TARGETS, ops_params->driver);
		req_params->depth = 1;
	} else {
		req_params->dirpath = spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
		req_params->depth = 2;
	}

	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	req_params->only_dirs = true;

	return 0;
}

static int
scst_target_enable_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct target_ops_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  ops_params->driver, ops_params->target, "enabled");
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("1");
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_target_disable_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct target_ops_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
					      ops_params->driver, ops_params->target, "enabled");
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("0");
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct group_ops_params {
	char *group;
	char *driver;
	char *target;
};

static const struct sto_ops_param_dsc group_ops_params_descriptors[] = {
	STO_OPS_PARAM_STR(group, struct group_ops_params, "SCST group name"),
	STO_OPS_PARAM_STR(driver, struct group_ops_params, "SCST target driver name"),
	STO_OPS_PARAM_STR(target, struct group_ops_params, "SCST target name"),
};

static const struct sto_ops_params_properties group_ops_params_properties =
	STO_OPS_PARAMS_INITIALIZER(group_ops_params_descriptors, struct group_ops_params);

static int
group_add_req_constructor(void *arg1, const void *arg2)
{
	struct scst_ini_group_params *req_params = arg1;
	struct group_ops_params *ops_params = (void *) arg2;

	req_params->driver_name = ops_params->driver;
	ops_params->driver = NULL;

	req_params->target_name = ops_params->target;
	ops_params->target = NULL;

	req_params->ini_group_name = ops_params->group;
	ops_params->group = NULL;

	return 0;
}

static void
ini_group_add_req_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct scst_ini_group_params *params = sto_req_get_params(req);

	scst_ini_group_add(params, sto_pipeline_step_done, pipe);
}

static void ini_group_del_req_step(struct sto_pipeline *pipe);

const struct sto_req_properties scst_ini_group_add_req_properties = {
	.params_size = sizeof(struct scst_ini_group_params),
	.params_deinit_fn = scst_ini_group_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(ini_group_add_req_step, ini_group_del_req_step),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

static int
ini_group_del_req_constructor(void *arg1, const void *arg2)
{
	return group_add_req_constructor(arg1, arg2);
}

static void
ini_group_del_req_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct scst_ini_group_params *params = sto_req_get_params(req);

	scst_ini_group_del(params, sto_pipeline_step_done, pipe);
}

const struct sto_req_properties scst_ini_group_del_req_properties = {
	.params_size = sizeof(struct scst_ini_group_params),
	.params_deinit_fn = scst_ini_group_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(ini_group_del_req_step, NULL),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

struct lun_add_ops_params {
	int lun;
	char *driver;
	char *target;
	char *device;
	char *group;
	char *attributes;
};

static const struct sto_ops_param_dsc lun_add_ops_params_descriptors[] = {
	STO_OPS_PARAM_INT32(lun, struct lun_add_ops_params, "LUN number"),
	STO_OPS_PARAM_STR(driver, struct lun_add_ops_params, "SCST target driver name"),
	STO_OPS_PARAM_STR(target, struct lun_add_ops_params, "SCST target name"),
	STO_OPS_PARAM_STR(device, struct lun_add_ops_params, "SCST device name"),
	STO_OPS_PARAM_STR_OPTIONAL(group, struct lun_add_ops_params, "SCST group name"),
	STO_OPS_PARAM_STR_OPTIONAL(attributes, struct lun_add_ops_params, "SCST device attributes <p=v,...>"),
};

static const struct sto_ops_params_properties lun_add_ops_params_properties =
	STO_OPS_PARAMS_INITIALIZER(lun_add_ops_params_descriptors, struct lun_add_ops_params);

static int
lun_add_req_constructor(void *arg1, const void *arg2)
{
	struct scst_lun_params *req_params = arg1;
	struct lun_add_ops_params *ops_params = (void *) arg2;

	req_params->lun_id = ops_params->lun;

	req_params->driver_name = ops_params->driver;
	ops_params->driver = NULL;

	req_params->target_name = ops_params->target;
	ops_params->target = NULL;

	req_params->ini_group_name = ops_params->group;
	ops_params->group = NULL;

	req_params->device_name = ops_params->device;
	ops_params->device = NULL;

	if (ops_params->attributes) {
		return scst_parse_attributes(ops_params->attributes, &req_params->attributes);
	}

	return 0;
}

static void
lun_add_req_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct scst_lun_params *params = sto_req_get_params(req);

	scst_lun_add(params, sto_pipeline_step_done, pipe);
}

static void lun_del_req_step(struct sto_pipeline *pipe);

const struct sto_req_properties lun_add_req_properties = {
	.params_size = sizeof(struct scst_lun_params),
	.params_deinit_fn = scst_lun_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(lun_add_req_step, lun_del_req_step),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

struct lun_del_ops_params {
	int lun;
	char *driver;
	char *target;
	char *group;
};

static const struct sto_ops_param_dsc lun_del_ops_params_descriptors[] = {
	STO_OPS_PARAM_INT32(lun, struct lun_del_ops_params, "LUN number"),
	STO_OPS_PARAM_STR(driver, struct lun_del_ops_params, "SCST target driver name"),
	STO_OPS_PARAM_STR(target, struct lun_del_ops_params, "SCST target name"),
	STO_OPS_PARAM_STR_OPTIONAL(group, struct lun_del_ops_params, "SCST group name"),
};

static const struct sto_ops_params_properties lun_del_ops_params_properties =
	STO_OPS_PARAMS_INITIALIZER(lun_del_ops_params_descriptors, struct lun_del_ops_params);

static int
lun_del_req_constructor(void *arg1, const void *arg2)
{
	struct scst_lun_params *req_params = arg1;
	struct lun_add_ops_params *ops_params = (void *) arg2;

	req_params->lun_id = ops_params->lun;

	req_params->driver_name = ops_params->driver;
	ops_params->driver = NULL;

	req_params->target_name = ops_params->target;
	ops_params->target = NULL;

	req_params->ini_group_name = ops_params->group;
	ops_params->group = NULL;

	return 0;
}

static void
lun_del_req_step(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct scst_lun_params *params = sto_req_get_params(req);

	scst_lun_del(params, sto_pipeline_step_done, pipe);
}

const struct sto_req_properties lun_del_req_properties = {
	.params_size = sizeof(struct scst_lun_params),
	.params_deinit_fn = scst_lun_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(lun_del_req_step, NULL),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};

static int
scst_lun_replace_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct lun_add_ops_params *ops_params = arg2;
	char *data;

	req_params->file = scst_target_lun_mgmt_path(ops_params->driver,
						     ops_params->target, ops_params->group);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	data = spdk_sprintf_alloc("replace %s %d", ops_params->device, ops_params->lun);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return -ENOMEM;
	}

	if (ops_params->attributes) {
		int rc = scst_parse_attributes(ops_params->attributes, &data);
		if (spdk_unlikely(rc)) {
			free(data);
			return rc;
		}
	}

	req_params->data = data;

	return 0;
}

struct scst_lun_clear_params {
	char *driver;
	char *target;
	char *group;
};

static const struct sto_ops_param_dsc scst_lun_clear_params_descriptors[] = {
	STO_OPS_PARAM_STR(driver, struct scst_lun_clear_params, "SCST target driver name"),
	STO_OPS_PARAM_STR(target, struct scst_lun_clear_params, "SCST target name"),
	STO_OPS_PARAM_STR_OPTIONAL(group, struct scst_lun_clear_params, "SCST group name"),
};

static const struct sto_ops_params_properties scst_lun_clear_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_lun_clear_params_descriptors, struct scst_lun_clear_params);

static int
scst_lun_clear_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_lun_clear_params *ops_params = arg2;

	req_params->file = scst_target_lun_mgmt_path(ops_params->driver,
						     ops_params->target, ops_params->group);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("clear");
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static const struct sto_ops scst_ops[] = {
	{
		.name = "snapshot",
		.description = "Dump the current SCST state to stdout",
		.req_properties = &snapshot_req_properties,
		.req_params_constructor = snapshot_req_constructor,
	},
	{
		.name = "handler_list",
		.description = "List all available handlers",
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_handler_list_constructor,
	},
	{
		.name = "driver_list",
		.description = "List all available drivers",
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_driver_list_constructor,
	},
	{
		.name = "dev_open",
		.description = "Adds a new device using handler <handler>",
		.params_properties = &dev_open_ops_params_properties,
		.req_properties = &dev_open_req_properties,
		.req_params_constructor = dev_open_req_constructor,
	},
	{
		.name = "dev_close",
		.description = "Closes a device belonging to handler <handler>",
		.params_properties = &dev_close_ops_params_properties,
		.req_properties = &dev_close_req_properties,
		.req_params_constructor = dev_close_req_constructor,
	},
	{
		.name = "dev_resync",
		.description = "Resync the device size with the initiator(s)",
		.params_properties = &scst_dev_resync_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dev_resync_constructor,
	},
	{
		.name = "dev_list",
		.description = "List all open devices",
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_dev_list_constructor,
	},
	{
		.name = "dgrp_add",
		.description = "Add device group <dgrp>",
		.params_properties = &scst_dgrp_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dgrp_add_constructor,
	},
	{
		.name = "dgrp_del",
		.description = "Remove device group <dgrp>",
		.params_properties = &scst_dgrp_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dgrp_del_constructor,
	},
	{
		.name = "dgrp_list",
		.description = "List all device groups",
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_dgrp_list_constructor,
	},
	{
		.name = "dgrp_add_dev",
		.description = "Add device <device> to device group <dgrp>",
		.params_properties = &scst_dgrp_dev_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dgrp_add_dev_constructor,
	},
	{
		.name = "dgrp_del_dev",
		.description = "Remove device <device> from device group <dgrp>",
		.params_properties = &scst_dgrp_dev_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dgrp_del_dev_constructor,
	},
	{
		.name = "tgrp_add",
		.description = "Add target group <tgrp> to device group <dgrp>",
		.params_properties = &scst_tgrp_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_tgrp_add_constructor,
	},
	{
		.name = "tgrp_del",
		.description = "Remove target group <tgrp> from device group <dgrp>",
		.params_properties = &scst_tgrp_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_tgrp_del_constructor,
	},
	{
		.name = "tgrp_list",
		.description = "List all target groups within a device group",
		.params_properties = &scst_tgrp_list_params_properties,
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_tgrp_list_constructor,
	},
	{
		.name = "tgrp_add_tgt",
		.description = "Add target <target> to specified target group",
		.params_properties = &scst_tgrp_tgt_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_tgrp_add_tgt_constructor,
	},
	{
		.name = "tgrp_del_tgt",
		.description = "Add target <target> to specified target group",
		.params_properties = &scst_tgrp_tgt_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_tgrp_del_tgt_constructor,
	},
	{
		.name = "target_add",
		.description = "Add a dynamic target to a capable driver",
		.params_properties = &target_ops_params_properties,
		.req_properties = &target_add_req_properties,
		.req_params_constructor = target_add_req_constructor,
	},
	{
		.name = "target_del",
		.description = "Remove a dynamic target from a driver",
		.params_properties = &target_ops_params_properties,
		.req_properties = &target_del_req_properties,
		.req_params_constructor = target_del_req_constructor,
	},
	{
		.name = "target_list",
		.description = "List all available targets",
		.params_properties = &scst_target_list_params_properties,
		.req_properties = &sto_tree_req_properties,
		.req_params_constructor = scst_target_list_constructor,
	},
	{
		.name = "target_enable",
		.description = "Enable target mode for a given driver & target",
		.params_properties = &target_ops_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_enable_constructor,
	},
	{
		.name = "target_disable",
		.description = "Disable target mode for a given driver & target",
		.params_properties = &target_ops_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_disable_constructor,
	},
	{
		.name = "group_add",
		.description = "Add a group to a given driver & target",
		.params_properties = &group_ops_params_properties,
		.req_properties = &scst_ini_group_add_req_properties,
		.req_params_constructor = group_add_req_constructor,
	},
	{
		.name = "group_del",
		.description = "Remove a group from a given driver & target",
		.params_properties = &group_ops_params_properties,
		.req_properties = &scst_ini_group_del_req_properties,
		.req_params_constructor = ini_group_del_req_constructor,
	},
	{
		.name = "lun_add",
		.description = "Adds a given device to a group",
		.params_properties = &lun_add_ops_params_properties,
		.req_properties = &lun_add_req_properties,
		.req_params_constructor = lun_add_req_constructor,
	},
	{
		.name = "lun_del",
		.description = "Remove a LUN from a group",
		.params_properties = &lun_del_ops_params_properties,
		.req_properties = &lun_del_req_properties,
		.req_params_constructor = lun_del_req_constructor,
	},
	{
		.name = "lun_replace",
		.description = "Adds a given device to a group",
		.params_properties = &lun_add_ops_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_lun_replace_constructor,
	},
	{
		.name = "lun_clear",
		.description = "Clear all LUNs within a group",
		.params_properties = &scst_lun_clear_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_lun_clear_constructor,
	},
};

static const struct sto_op_table scst_op_table = STO_OP_TABLE_INITIALIZER(scst_ops);

static void
scst_subsystem_init_done(void *cb_arg, int rc)
{
	SPDK_ERRLOG("SCST subsystem init done: rc=%d\n", rc);

	sto_subsystem_init_next(rc);
}

static void
scst_subsystem_init(void)
{
	SPDK_ERRLOG("SCST subsystem init start\n");

	scst_init(scst_subsystem_init_done, NULL);
}

static void
scst_subsystem_fini_done(void *cb_arg, int rc)
{
	SPDK_ERRLOG("SCST subsystem fini done: rc=%d\n", rc);

	sto_subsystem_fini_next();
}

static void
scst_subsystem_fini(void)
{
	SPDK_ERRLOG("SCST subsystem fini start\n");

	scst_fini(scst_subsystem_fini_done, NULL);
}

static struct sto_subsystem_ops scst_subsystem_ops = {
	.init = scst_subsystem_init,
	.fini = scst_subsystem_fini,
};

STO_SUBSYSTEM_REGISTER(scst, &scst_op_table, &scst_subsystem_ops);
