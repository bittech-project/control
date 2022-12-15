#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_string_fns.h>

#include "sto_lib.h"
#include "sto_subsystem.h"

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

static char *
scst_attr(const char *buf, bool nonkey)
{
	char *tmp_buf, *attr = NULL;
#define SCST_ATTR_MAX_LINES 3
	char *lines[SCST_ATTR_MAX_LINES] = {};
	int ret;

	if (spdk_unlikely(!buf)) {
		return NULL;
	}

	tmp_buf = strdup(buf);
	if (spdk_unlikely(!tmp_buf)) {
		SPDK_ERRLOG("Failed to alloc buf for SCST attr\n");
		return NULL;
	}

	ret = rte_strsplit(tmp_buf, strlen(tmp_buf), lines, SPDK_COUNTOF(lines), '\n');
	if (spdk_unlikely(ret == -1)) {
		SPDK_ERRLOG("Failed to split scst attr\n");
		goto out;
	}

	if (!ret) {
		goto out;
	}

	if (!nonkey && (!lines[1] || strcmp(lines[1], "[key]"))) {
		goto out;
	}

	attr = strdup(lines[0]);

out:
	free(tmp_buf);

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
		goto out;
	}

	spdk_json_write_named_string(w, attr_inode->name, attr);

out:
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
scst_snapshot_constructor(void *arg1, void *arg2)
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
scst_handler_list_constructor(void *arg1, void *arg2)
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
scst_driver_list_constructor(void *arg1, void *arg2)
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

	free(parsed_attributes);
out:
	return rc;
}

struct scst_dev_open_params {
	char *name;
	char *handler;
	char *attributes;
};

static void
scst_dev_open_params_deinit(void *arg)
{
	struct scst_dev_open_params *params = arg;

	free(params->name);
	free(params->handler);
	free(params->attributes);
}

static const struct spdk_json_object_decoder scst_dev_open_decoders[] = {
	{"name", offsetof(struct scst_dev_open_params, name), spdk_json_decode_string},
	{"handler", offsetof(struct scst_dev_open_params, handler), spdk_json_decode_string},
	{"attributes", offsetof(struct scst_dev_open_params, attributes), spdk_json_decode_string, true},
};

const struct sto_ops_decoder scst_dev_open_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_dev_open_decoders,
				    sizeof(struct scst_dev_open_params),
				    scst_dev_open_params_deinit);

static int
scst_dev_open_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_dev_open_params *ops_params = arg2;
	char *data;
	int rc;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
					      ops_params->handler, SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	data = spdk_sprintf_alloc("add_device %s", ops_params->name);
	if (spdk_unlikely(!data)) {
		return -ENOMEM;
	}

	rc = scst_parse_attributes(ops_params->attributes, &data);
	if (spdk_unlikely(rc)) {
		free(data);
		return -ENOMEM;
	}

	req_params->data = data;

	return 0;
}

struct scst_dev_close_params {
	char *name;
	char *handler;
};

static void
scst_dev_close_params_deinit(void *arg)
{
	struct scst_dev_close_params *params = arg;

	free(params->name);
	free(params->handler);
}

static const struct spdk_json_object_decoder scst_dev_close_decoders[] = {
	{"name", offsetof(struct scst_dev_close_params, name), spdk_json_decode_string},
	{"handler", offsetof(struct scst_dev_close_params, handler), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_dev_close_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_dev_close_decoders,
				    sizeof(struct scst_dev_close_params),
				    scst_dev_close_params_deinit);

static int
scst_dev_close_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_dev_open_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
					      ops_params->handler, SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del_device %s", ops_params->name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct scst_dev_resync_params {
	char *name;
};

static void
scst_dev_resync_params_deinit(void *arg)
{
	struct scst_dev_resync_params *params = arg;
	free(params->name);
}

static const struct spdk_json_object_decoder scst_dev_resync_decoders[] = {
	{"name", offsetof(struct scst_dev_resync_params, name), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_dev_resync_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_dev_resync_decoders,
				    sizeof(struct scst_dev_resync_params),
				    scst_dev_resync_params_deinit);

static int
scst_dev_resync_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_dev_resync_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEVICES,
					      ops_params->name, "resync_size");
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
scst_dev_list_constructor(void *arg1, void *arg2)
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
	char *name;
};

static void
scst_dgrp_params_deinit(void *arg)
{
	struct scst_dgrp_params *params = arg;
	free(params->name);
}

static const struct spdk_json_object_decoder scst_dgrp_decoders[] = {
	{"name", offsetof(struct scst_dgrp_params, name), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_dgrp_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_dgrp_decoders,
				    sizeof(struct scst_dgrp_params),
				    scst_dgrp_params_deinit);

static int
scst_dgrp_add_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_dgrp_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS, SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("create %s", ops_params->name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_dgrp_del_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_dgrp_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS, SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_dgrp_list_constructor(void *arg1, void *arg2)
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
	char *name;
	char *dev_name;
};

static void
scst_dgrp_dev_params_deinit(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;

	free(params->name);
	free(params->dev_name);
}

static const struct spdk_json_object_decoder scst_dgrp_dev_decoders[] = {
	{"name", offsetof(struct scst_dgrp_dev_params, name), spdk_json_decode_string},
	{"dev_name", offsetof(struct scst_dgrp_dev_params, dev_name), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_dgrp_dev_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_dgrp_dev_decoders,
				    sizeof(struct scst_dgrp_dev_params),
				    scst_dgrp_dev_params_deinit);

static int
scst_dgrp_add_dev_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_dgrp_dev_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
					      ops_params->name, "devices", SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("add %s", ops_params->dev_name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_dgrp_del_dev_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_dgrp_dev_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
					      ops_params->name, "devices", SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->dev_name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct scst_tgrp_params {
	char *name;
	char *dgrp_name;
};

static void
scst_tgrp_params_deinit(void *arg)
{
	struct scst_tgrp_params *params = arg;

	free(params->name);
	free(params->dgrp_name);
}

static const struct spdk_json_object_decoder scst_tgrp_decoders[] = {
	{"name", offsetof(struct scst_tgrp_params, name), spdk_json_decode_string},
	{"dgrp_name", offsetof(struct scst_tgrp_params, dgrp_name), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_tgrp_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_tgrp_decoders,
				    sizeof(struct scst_tgrp_params),
				    scst_tgrp_params_deinit);

static int
scst_tgrp_add_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_tgrp_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
					      ops_params->dgrp_name, "target_groups", SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("add %s", ops_params->name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_tgrp_del_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_tgrp_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
					      ops_params->dgrp_name, "target_groups", SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct scst_tgrp_list_params {
	char *dgrp;
};

static void
scst_tgrp_list_params_deinit(void *arg)
{
	struct scst_tgrp_list_params *params = arg;

	free(params->dgrp);
}

static const struct spdk_json_object_decoder scst_tgrp_list_decoders[] = {
	{"dgrp", offsetof(struct scst_tgrp_list_params, dgrp), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_tgrp_list_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_tgrp_list_decoders,
				    sizeof(struct scst_tgrp_list_params),
				    scst_tgrp_list_params_deinit);

static int
scst_tgrp_list_constructor(void *arg1, void *arg2)
{
	struct sto_readdir_req_params *req_params = arg1;
	struct scst_tgrp_list_params *ops_params = arg2;

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
	char *tgt_name;
	char *dgrp_name;
	char *tgrp_name;
};

static void
scst_tgrp_tgt_params_deinit(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;

	free(params->tgt_name);
	free(params->dgrp_name);
	free(params->tgrp_name);
}

static const struct spdk_json_object_decoder scst_tgrp_tgt_decoders[] = {
	{"tgt_name", offsetof(struct scst_tgrp_tgt_params, tgt_name), spdk_json_decode_string},
	{"dgrp_name", offsetof(struct scst_tgrp_tgt_params, dgrp_name), spdk_json_decode_string},
	{"tgrp_name", offsetof(struct scst_tgrp_tgt_params, tgrp_name), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_tgrp_tgt_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_tgrp_tgt_decoders,
				    sizeof(struct scst_tgrp_tgt_params),
				    scst_tgrp_tgt_params_deinit);

static int
scst_tgrp_add_tgt_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_tgrp_tgt_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
					      ops_params->dgrp_name, "target_groups",
					      ops_params->tgrp_name, SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("add %s", ops_params->tgt_name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static int
scst_tgrp_del_tgt_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_tgrp_tgt_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
					      ops_params->dgrp_name, "target_groups",
					      ops_params->tgrp_name, SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->tgt_name);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct scst_target_params {
	char *target;
	char *driver;
};

static void
scst_target_params_deinit(void *arg)
{
	struct scst_target_params *params = arg;

	free(params->target);
	free(params->driver);
}

static const struct spdk_json_object_decoder scst_target_decoders[] = {
	{"target", offsetof(struct scst_target_params, target), spdk_json_decode_string},
	{"driver", offsetof(struct scst_target_params, driver), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_target_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_target_decoders,
				    sizeof(struct scst_target_params),
				    scst_target_params_deinit);

static int
scst_target_add_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_target_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
					      ops_params->driver, SCST_MGMT_IO);
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
scst_target_del_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_target_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
					      ops_params->driver, SCST_MGMT_IO);
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

static void
scst_target_list_params_deinit(void *arg)
{
	struct scst_target_list_params *params = arg;

	free(params->driver);
}

static const struct spdk_json_object_decoder scst_target_list_decoders[] = {
	{"driver", offsetof(struct scst_target_list_params, driver), spdk_json_decode_string, true},
};

const struct sto_ops_decoder scst_target_list_decoder =
	STO_OPS_DECODER_INITIALIZER_EMPTY(scst_target_list_decoders,
					  sizeof(struct scst_target_list_params),
					  scst_target_list_params_deinit);

static int
scst_target_list_constructor(void *arg1, void *arg2)
{
	struct sto_tree_req_params *req_params = arg1;
	struct scst_target_list_params *ops_params = arg2;

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
scst_target_enable_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_target_params *ops_params = arg2;

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
scst_target_disable_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_target_params *ops_params = arg2;

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

static void
scst_group_params_deinit(void *arg)
{
	struct scst_group_params *params = arg;

	free(params->group);
	free(params->driver);
	free(params->target);
}

static const struct spdk_json_object_decoder scst_group_decoders[] = {
	{"group", offsetof(struct scst_group_params, group), spdk_json_decode_string},
	{"driver", offsetof(struct scst_group_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_group_params, target), spdk_json_decode_string},
};

const struct sto_ops_decoder scst_group_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_group_decoders,
				    sizeof(struct scst_group_params),
				    scst_group_params_deinit);

static int
scst_group_add_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_group_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
					      ops_params->driver, ops_params->target,
					      "ini_groups", SCST_MGMT_IO);
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
scst_group_del_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_group_params *ops_params = arg2;

	req_params->file = spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
					      ops_params->driver, ops_params->target,
					      "ini_groups", SCST_MGMT_IO);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = spdk_sprintf_alloc("del %s", ops_params->group);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

static const char *
scst_lun_mgmt_file_constructor(char *driver, char *target, char *group)
{
	if (group) {
		return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s/%s/%s", SCST_ROOT,
					  SCST_TARGETS, driver,
					  target, "ini_groups", group,
					  "luns", SCST_MGMT_IO);
	}

	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT,
				  SCST_TARGETS, driver,
				  target, "luns", SCST_MGMT_IO);
}

struct scst_lun_add_params {
	int lun;
	char *driver;
	char *target;
	char *device;
	char *group;
	char *attributes;
};

static void
scst_lun_add_params_deinit(void *arg)
{
	struct scst_lun_add_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->device);
	free(params->group);
	free(params->attributes);
}

static const struct spdk_json_object_decoder scst_lun_add_decoders[] = {
	{"lun", offsetof(struct scst_lun_add_params, lun), spdk_json_decode_int32},
	{"driver", offsetof(struct scst_lun_add_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_add_params, target), spdk_json_decode_string},
	{"device", offsetof(struct scst_lun_add_params, device), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_add_params, group), spdk_json_decode_string, true},
	{"attributes", offsetof(struct scst_lun_add_params, attributes), spdk_json_decode_string, true},
};

const struct sto_ops_decoder scst_lun_add_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_lun_add_decoders,
				    sizeof(struct scst_lun_add_params),
				    scst_lun_add_params_deinit);

static int
scst_lun_add_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_lun_add_params *ops_params = arg2;
	char *data;
	int rc;

	req_params->file = scst_lun_mgmt_file_constructor(ops_params->driver,
							  ops_params->target, ops_params->group);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	data = spdk_sprintf_alloc("add %s %d", ops_params->device, ops_params->lun);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return -ENOMEM;
	}

	rc = scst_parse_attributes(ops_params->attributes, &data);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to fill scst attrs\n");
		free(data);
		return -ENOMEM;
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

static void
scst_lun_del_params_deinit(void *arg)
{
	struct scst_lun_del_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->group);
}

static const struct spdk_json_object_decoder scst_lun_del_decoders[] = {
	{"lun", offsetof(struct scst_lun_add_params, lun), spdk_json_decode_int32},
	{"driver", offsetof(struct scst_lun_del_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_del_params, target), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_del_params, group), spdk_json_decode_string, true},
};

const struct sto_ops_decoder scst_lun_del_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_lun_del_decoders,
				    sizeof(struct scst_lun_del_params),
				    scst_lun_del_params_deinit);

static int
scst_lun_del_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_lun_del_params *ops_params = arg2;

	req_params->file = scst_lun_mgmt_file_constructor(ops_params->driver,
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
scst_lun_replace_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_lun_add_params *ops_params = arg2;
	char *data;
	int rc;

	req_params->file = scst_lun_mgmt_file_constructor(ops_params->driver,
							  ops_params->target, ops_params->group);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	data = spdk_sprintf_alloc("replace %s %d", ops_params->device, ops_params->lun);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return -ENOMEM;
	}

	rc = scst_parse_attributes(ops_params->attributes, &data);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to fill scst attrs\n");
		free(data);
		return -ENOMEM;
	}

	req_params->data = data;

	return 0;
}

struct scst_lun_clear_params {
	char *driver;
	char *target;
	char *group;
};

static void
scst_lun_clear_params_deinit(void *arg)
{
	struct scst_lun_clear_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->group);
}

static const struct spdk_json_object_decoder scst_lun_clear_decoders[] = {
	{"driver", offsetof(struct scst_lun_clear_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_clear_params, target), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_clear_params, group), spdk_json_decode_string, true},
};

struct sto_ops_decoder scst_lun_clear_decoder =
	STO_OPS_DECODER_INITIALIZER(scst_lun_clear_decoders,
				    sizeof(struct scst_lun_clear_params),
				    scst_lun_clear_params_deinit);

static int
scst_lun_clear_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct scst_lun_clear_params *ops_params = arg2;

	req_params->file = scst_lun_mgmt_file_constructor(ops_params->driver,
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
		.req_properties = &sto_tree_req_properties,
		.req_params_constructor = scst_snapshot_constructor,
	},
	{
		.name = "handler_list",
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_handler_list_constructor,
	},
	{
		.name = "driver_list",
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_driver_list_constructor,
	},
	{
		.name = "dev_open",
		.decoder = &scst_dev_open_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dev_open_constructor,
	},
	{
		.name = "dev_close",
		.decoder = &scst_dev_close_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dev_close_constructor,
	},
	{
		.name = "dev_resync",
		.decoder = &scst_dev_resync_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dev_resync_constructor,
	},
	{
		.name = "dev_list",
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_dev_list_constructor,
	},
	{
		.name = "dgrp_add",
		.decoder = &scst_dgrp_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dgrp_add_constructor,
	},
	{
		.name = "dgrp_del",
		.decoder = &scst_dgrp_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dgrp_del_constructor,
	},
	{
		.name = "dgrp_list",
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_dgrp_list_constructor,
	},
	{
		.name = "dgrp_add_dev",
		.decoder = &scst_dgrp_dev_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dgrp_add_dev_constructor,
	},
	{
		.name = "dgrp_del_dev",
		.decoder = &scst_dgrp_dev_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_dgrp_del_dev_constructor,
	},
	{
		.name = "tgrp_add",
		.decoder = &scst_tgrp_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_tgrp_add_constructor,
	},
	{
		.name = "tgrp_del",
		.decoder = &scst_tgrp_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_tgrp_del_constructor,
	},
	{
		.name = "tgrp_list",
		.decoder = &scst_tgrp_list_decoder,
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = scst_tgrp_list_constructor,
	},
	{
		.name = "tgrp_add_tgt",
		.decoder = &scst_tgrp_tgt_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_tgrp_add_tgt_constructor,
	},
	{
		.name = "tgrp_del_tgt",
		.decoder = &scst_tgrp_tgt_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_tgrp_del_tgt_constructor,
	},
	{
		.name = "target_add",
		.decoder = &scst_target_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_add_constructor,
	},
	{
		.name = "target_del",
		.decoder = &scst_target_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_del_constructor,
	},
	{
		.name = "target_list",
		.decoder = &scst_target_list_decoder,
		.req_properties = &sto_tree_req_properties,
		.req_params_constructor = scst_target_list_constructor,
	},
	{
		.name = "target_enable",
		.decoder = &scst_target_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_enable_constructor,
	},
	{
		.name = "target_disable",
		.decoder = &scst_target_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_target_disable_constructor,
	},
	{
		.name = "group_add",
		.decoder = &scst_group_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_group_add_constructor,
	},
	{
		.name = "group_del",
		.decoder = &scst_group_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_group_del_constructor,
	},
	{
		.name = "lun_add",
		.decoder = &scst_lun_add_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_lun_add_constructor,
	},
	{
		.name = "lun_del",
		.decoder = &scst_lun_del_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_lun_del_constructor,
	},
	{
		.name = "lun_replace",
		.decoder = &scst_lun_add_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_lun_replace_constructor,
	},
	{
		.name = "lun_clear",
		.decoder = &scst_lun_clear_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = scst_lun_clear_constructor,
	},
};

static const struct sto_op_table scst_op_table = STO_OP_TABLE_INITIALIZER(scst_ops);

static struct sto_subsystem g_scst_subsystem = STO_SUBSYSTEM_INITIALIZER("scst", &scst_op_table);
STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
