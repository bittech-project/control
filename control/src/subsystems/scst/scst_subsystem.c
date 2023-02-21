#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_generic_req.h"
#include "sto_subsystem.h"

#include "scst.h"

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

static char *
scst_attr(const char *buf, bool nonkey)
{
	char *attr = NULL;
	char **lines;

	if (spdk_unlikely(!buf)) {
		return NULL;
	}

	lines = spdk_strarray_from_string(buf, "\n");
	if (spdk_unlikely(!lines)) {
		SPDK_ERRLOG("Failed to split scst attr\n");
		return NULL;
	}

	if (!nonkey && (!lines[1] || strcmp(lines[1], "[key]"))) {
		goto out;
	}

	attr = strdup(lines[0]);

out:
	spdk_strarray_free(lines);

	return attr;
}

static void
scst_serialize_attr(struct sto_inode *attr_inode, struct spdk_json_write_ctx *w)
{
	char *attr;

	if (sto_inode_read_only(attr_inode)) {
		return;
	}

	attr = scst_attr(sto_file_inode_buf(attr_inode), false);
	if (!attr) {
		return;
	}

	spdk_json_write_named_string(w, attr_inode->name, attr);

	free(attr);
}

static void
scst_serialize_attrs(struct sto_tree_node *obj_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *attr_node;

	STO_TREE_FOREACH_TYPE(attr_node, obj_node, STO_INODE_TYPE_FILE) {
		struct sto_inode *inode = attr_node->inode;

		scst_serialize_attr(inode, w);
	}
}

static void
scst_snapshot_dev_info_json(struct sto_tree_node *dev_lnk_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *dev_node;

	dev_node = sto_tree_node_resolv_lnk(dev_lnk_node);
	if (spdk_unlikely(!dev_node)) {
		SPDK_ERRLOG("Failed to resolve device %s link\n",
			    dev_lnk_node->inode->name);
		return;
	}

	spdk_json_write_name(w, dev_lnk_node->inode->name);

	spdk_json_write_object_begin(w);

	scst_serialize_attrs(dev_node, w);

	spdk_json_write_object_end(w);
}

static void
scst_snapshot_handler_info_json(struct sto_tree_node *dh_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *dev_node, *mgmt_node;

	mgmt_node = sto_tree_node_find(dh_node, "mgmt");
	if (spdk_unlikely(!mgmt_node)) {
		SPDK_ERRLOG("Failed to find 'mgmt' for handler %s\n",
			    dh_node->inode->name);
		return;
	}

	spdk_json_write_name(w, dh_node->inode->name);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "devices");

	STO_TREE_FOREACH_TYPE(dev_node, dh_node, STO_INODE_TYPE_LNK) {
		spdk_json_write_object_begin(w);
		scst_snapshot_dev_info_json(dev_node, w);
		spdk_json_write_object_end(w);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}

static bool
scst_snapshot_handlers_is_empty(struct sto_tree_node *handlers_node)
{
	struct sto_tree_node *dh_node;

	if (sto_tree_node_first_child_type(handlers_node, STO_INODE_TYPE_DIR) == NULL) {
		return true;
	}

	STO_TREE_FOREACH_TYPE(dh_node, handlers_node, STO_INODE_TYPE_DIR) {
		if (sto_tree_node_first_child_type(dh_node, STO_INODE_TYPE_LNK) != NULL) {
			return false;
		}
	}

	return true;
}

static void
scst_snapshot_handlers_info_json(struct sto_tree_node *tree_root, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *handlers_node, *dh_node;

	SPDK_ERRLOG("GLEB: SCST config handlers info json\n");

	handlers_node = sto_tree_node_find(tree_root, "handlers");

	if (!handlers_node || scst_snapshot_handlers_is_empty(handlers_node)) {
		return;
	}

	spdk_json_write_named_array_begin(w, "handlers");

	STO_TREE_FOREACH_TYPE(dh_node, handlers_node, STO_INODE_TYPE_DIR) {
		if (sto_tree_node_first_child_type(dh_node, STO_INODE_TYPE_LNK) == NULL) {
			continue;
		}

		spdk_json_write_object_begin(w);
		scst_snapshot_handler_info_json(dh_node, w);
		spdk_json_write_object_end(w);
	}

	spdk_json_write_array_end(w);
}

static void
scst_snapshot_info_json(struct sto_tree_node *tree_root, struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	scst_snapshot_handlers_info_json(tree_root, w);

	spdk_json_write_object_end(w);
}

static int
scst_snapshot_constructor(void *arg1, const void *arg2)
{
	struct sto_tree_req_params *req_params = arg1;

	req_params->dirpath = spdk_sprintf_alloc("%s", SCST_ROOT);
	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	req_params->info_json = scst_snapshot_info_json;

	return 0;
}

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

struct scst_dev_open_params {
	char *device;
	char *handler;
	char *attributes;
};

static const struct sto_ops_param_dsc scst_dev_open_params_descriptors[] = {
	STO_OPS_PARAM_STR(device, struct scst_dev_open_params, "SCST device name"),
	STO_OPS_PARAM_STR(handler, struct scst_dev_open_params, "SCST handler name"),
	STO_OPS_PARAM_STR_OPTIONAL(attributes, struct scst_dev_open_params, "SCST device attributes <p=v,...>"),
};

static const struct sto_ops_params_properties scst_dev_open_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_dev_open_params_descriptors, struct scst_dev_open_params);

static int
scst_dev_open_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_dev_open_params *ops_params = arg2;
	char *data;

	req_params->file = scst_handler_mgmt(ops_params->handler);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	data = spdk_sprintf_alloc("add_device %s", ops_params->device);
	if (spdk_unlikely(!data)) {
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

struct scst_dev_close_params {
	char *device;
	char *handler;
};

static const struct sto_ops_param_dsc scst_dev_close_params_descriptors[] = {
	STO_OPS_PARAM_STR(device, struct scst_dev_open_params, "SCST device name"),
	STO_OPS_PARAM_STR(handler, struct scst_dev_open_params, "SCST handler name"),
};

static const struct sto_ops_params_properties scst_dev_close_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_dev_close_params_descriptors, struct scst_dev_close_params);

static int
scst_dev_close_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_dev_open_params *ops_params = arg2;

	req_params->file = scst_handler_mgmt(ops_params->handler);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del_device %s", ops_params->device);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

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

struct scst_target_params {
	char *target;
	char *driver;
};

static const struct sto_ops_param_dsc scst_target_params_descriptors[] = {
	STO_OPS_PARAM_STR(target, struct scst_target_params, "SCST target name"),
	STO_OPS_PARAM_STR(driver, struct scst_target_params, "SCST target driver name"),
};

static const struct sto_ops_params_properties scst_target_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_target_params_descriptors, struct scst_target_params);

static int
scst_target_add_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_target_params *ops_params = arg2;

	req_params->file = scst_target_driver_mgmt(ops_params->driver);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("add_target %s", ops_params->target);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_target_del_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_target_params *ops_params = arg2;

	req_params->file = scst_target_driver_mgmt(ops_params->driver);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del_target %s", ops_params->target);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

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
	const struct scst_target_params *ops_params = arg2;

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
	const struct scst_target_params *ops_params = arg2;

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

struct scst_group_params {
	char *group;
	char *driver;
	char *target;
};

static const struct sto_ops_param_dsc scst_group_params_descriptors[] = {
	STO_OPS_PARAM_STR(group, struct scst_group_params, "SCST group name"),
	STO_OPS_PARAM_STR(driver, struct scst_group_params, "SCST target driver name"),
	STO_OPS_PARAM_STR(target, struct scst_group_params, "SCST target name"),
};

static const struct sto_ops_params_properties scst_group_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_group_params_descriptors, struct scst_group_params);

static int
scst_group_add_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_group_params *ops_params = arg2;

	req_params->file = scst_target_ini_groups_mgmt(ops_params->driver, ops_params->target);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("create %s", ops_params->group);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_group_del_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_group_params *ops_params = arg2;

	req_params->file = scst_target_ini_groups_mgmt(ops_params->driver, ops_params->target);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->group);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct scst_lun_add_params {
	int lun;
	char *driver;
	char *target;
	char *device;
	char *group;
	char *attributes;
};

static const struct sto_ops_param_dsc scst_lun_add_params_descriptors[] = {
	STO_OPS_PARAM_INT32(lun, struct scst_lun_add_params, "LUN number"),
	STO_OPS_PARAM_STR(driver, struct scst_lun_add_params, "SCST target driver name"),
	STO_OPS_PARAM_STR(target, struct scst_lun_add_params, "SCST target name"),
	STO_OPS_PARAM_STR(device, struct scst_lun_add_params, "SCST device name"),
	STO_OPS_PARAM_STR(group, struct scst_lun_add_params, "SCST group name"),
	STO_OPS_PARAM_STR_OPTIONAL(attributes, struct scst_lun_add_params, "SCST device attributes <p=v,...>"),
};

static const struct sto_ops_params_properties scst_lun_add_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_lun_add_params_descriptors, struct scst_lun_add_params);

static int
scst_lun_add_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_lun_add_params *ops_params = arg2;
	char *data;

	req_params->file = scst_target_lun_mgmt(ops_params->driver,
						ops_params->target, ops_params->group);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	data = spdk_sprintf_alloc("add %s %d", ops_params->device, ops_params->lun);
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

struct scst_lun_del_params {
	int lun;
	char *driver;
	char *target;
	char *group;
};

static const struct sto_ops_param_dsc scst_lun_del_params_descriptors[] = {
	STO_OPS_PARAM_INT32(lun, struct scst_lun_del_params, "LUN number"),
	STO_OPS_PARAM_STR(driver, struct scst_lun_del_params, "SCST target driver name"),
	STO_OPS_PARAM_STR(target, struct scst_lun_del_params, "SCST target name"),
	STO_OPS_PARAM_STR_OPTIONAL(group, struct scst_lun_del_params, "SCST group name"),
};

static const struct sto_ops_params_properties scst_lun_del_params_properties =
	STO_OPS_PARAMS_INITIALIZER(scst_lun_del_params_descriptors, struct scst_lun_del_params);

static int
scst_lun_del_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_lun_del_params *ops_params = arg2;

	req_params->file = scst_target_lun_mgmt(ops_params->driver,
						ops_params->target, ops_params->group);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %d", ops_params->lun);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_lun_replace_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct scst_lun_add_params *ops_params = arg2;
	char *data;

	req_params->file = scst_target_lun_mgmt(ops_params->driver,
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

	req_params->file = scst_target_lun_mgmt(ops_params->driver,
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
		.req_properties = &sto_tree_req_properties,
		.req_params_constructor = scst_snapshot_constructor,
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
		.params_properties = &scst_dev_open_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dev_open_constructor,
	},
	{
		.name = "dev_close",
		.description = "Closes a device belonging to handler <handler>",
		.params_properties = &scst_dev_close_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dev_close_constructor,
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
		.params_properties = &scst_target_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_add_constructor,
	},
	{
		.name = "target_del",
		.description = "Remove a dynamic target from a driver",
		.params_properties = &scst_target_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_del_constructor,
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
		.params_properties = &scst_target_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_enable_constructor,
	},
	{
		.name = "target_disable",
		.description = "Disable target mode for a given driver & target",
		.params_properties = &scst_target_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_disable_constructor,
	},
	{
		.name = "group_add",
		.description = "Add a group to a given driver & target",
		.params_properties = &scst_group_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_group_add_constructor,
	},
	{
		.name = "group_del",
		.description = "Remove a group from a given driver & target",
		.params_properties = &scst_group_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_group_del_constructor,
	},
	{
		.name = "lun_add",
		.description = "Adds a given device to a group",
		.params_properties = &scst_lun_add_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_lun_add_constructor,
	},
	{
		.name = "lun_del",
		.description = "Remove a LUN from a group",
		.params_properties = &scst_lun_del_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_lun_del_constructor,
	},
	{
		.name = "lun_replace",
		.description = "Adds a given device to a group",
		.params_properties = &scst_lun_add_params_properties,
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

STO_SUBSYSTEM_REGISTER(scst, &scst_op_table);
