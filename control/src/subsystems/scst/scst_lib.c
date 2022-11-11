#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_lib.h"

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
scst_config_write_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s", SCST_ROOT);
}

static struct sto_tree_req_params_constructor config_write_constructor = {
	.dirpath = scst_config_write_dirpath,
};

static const char *
scst_handler_list_name(void *arg)
{
	return spdk_sprintf_alloc("handlers");
}

static char *
scst_handler_list_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_HANDLERS);
}

static struct sto_readdir_req_params_constructor handler_list_constructor = {
	.name = scst_handler_list_name,
	.dirpath = scst_handler_list_dirpath,
};

static const char *
scst_driver_list_name(void *arg)
{
	return spdk_sprintf_alloc("Drivers");
}

static char *
scst_driver_list_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
}

static struct sto_readdir_req_params_constructor driver_list_constructor = {
	.name = scst_driver_list_name,
	.dirpath = scst_driver_list_dirpath,
};

#define SCST_DEV_MAX_ATTR_CNT 32
struct scst_attr_name_list {
	const char *names[SCST_DEV_MAX_ATTR_CNT];
	size_t cnt;
};

static int
scst_attr_list_decode(const struct spdk_json_val *val, void *out)
{
	struct scst_attr_name_list *attr_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, attr_list->names,
				      SCST_DEV_MAX_ATTR_CNT, &attr_list->cnt, sizeof(char *));
}

static void
scst_attr_list_free(struct scst_attr_name_list *attr_list)
{
	ssize_t i;

	for (i = 0; i < attr_list->cnt; i++) {
		free((char *) attr_list->names[i]);
	}
}

static int
scst_attr_list_fill_data(struct scst_attr_name_list *attr_list, char **data)
{
	char *parsed_cmd;
	int i;

	for (i = 0; i < attr_list->cnt; i++) {
		parsed_cmd = spdk_sprintf_append_realloc(*data, " %s;",
							 attr_list->names[i]);
		if (spdk_unlikely(!parsed_cmd)) {
			SPDK_ERRLOG("Failed to realloc memory for attributes data\n");
			return -ENOMEM;
		}

		*data = parsed_cmd;
	}

	return 0;
}

struct scst_dev_open_params {
	char *name;
	char *handler;
	struct scst_attr_name_list attr_list;
};

static void *
scst_dev_open_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dev_open_params));
}

static void
scst_dev_open_params_free(void *arg)
{
	struct scst_dev_open_params *params = arg;

	free(params->name);
	free(params->handler);
	scst_attr_list_free(&params->attr_list);
	free(params);
}

static const struct spdk_json_object_decoder scst_dev_open_decoders[] = {
	{"name", offsetof(struct scst_dev_open_params, name), spdk_json_decode_string},
	{"handler", offsetof(struct scst_dev_open_params, handler), spdk_json_decode_string},
	{"attributes", offsetof(struct scst_dev_open_params, attr_list), scst_attr_list_decode, true},
};

static const char *
scst_dev_open_mgmt_file_path(void *arg)
{
	struct scst_dev_open_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
				  params->handler, SCST_MGMT_IO);
}

static char *
scst_dev_open_data(void *arg)
{
	struct scst_dev_open_params *params = arg;
	char *data;
	int rc;

	data = spdk_sprintf_alloc("add_device %s", params->name);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return NULL;
	}

	rc = scst_attr_list_fill_data(&params->attr_list, &data);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to fill scst attrs\n");
		free(data);
		return NULL;
	}

	return data;
}

static struct sto_write_req_params_constructor dev_open_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dev_open_decoders,
					   scst_dev_open_params_alloc, scst_dev_open_params_free),
	.file_path = scst_dev_open_mgmt_file_path,
	.data = scst_dev_open_data,
};

struct scst_dev_close_params {
	char *name;
	char *handler;
};

static void *
scst_dev_close_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dev_close_params));
}

static void
scst_dev_close_params_free(void *arg)
{
	struct scst_dev_close_params *params = arg;

	free(params->name);
	free(params->handler);
	free(params);
}

static const struct spdk_json_object_decoder scst_dev_close_decoders[] = {
	{"name", offsetof(struct scst_dev_close_params, name), spdk_json_decode_string},
	{"handler", offsetof(struct scst_dev_close_params, handler), spdk_json_decode_string},
};

static const char *
scst_dev_close_mgmt_file_path(void *arg)
{
	struct scst_dev_close_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_HANDLERS,
				  params->handler, SCST_MGMT_IO);
}

static char *
scst_dev_close_data(void *arg)
{
	struct scst_dev_close_params *params = arg;
	return spdk_sprintf_alloc("del_device %s", params->name);
}

static struct sto_write_req_params_constructor dev_close_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dev_close_decoders,
					   scst_dev_close_params_alloc, scst_dev_close_params_free),
	.file_path = scst_dev_close_mgmt_file_path,
	.data = scst_dev_close_data,
};

struct scst_dev_resync_params {
	char *name;
};

static void *
scst_dev_resync_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dev_resync_params));
}

static void
scst_dev_resync_params_free(void *arg)
{
	struct scst_dev_resync_params *params = arg;
	free(params->name);
	free(params);
}

static const struct spdk_json_object_decoder scst_dev_resync_decoders[] = {
	{"name", offsetof(struct scst_dev_resync_params, name), spdk_json_decode_string},
};

static const char *
scst_dev_resync_mgmt_file_path(void *arg)
{
	struct scst_dev_resync_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEVICES,
				  params->name, "resync_size");
}

static char *
scst_dev_resync_data(void *arg)
{
	return spdk_sprintf_alloc("1");
}

static struct sto_write_req_params_constructor dev_resync_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dev_resync_decoders,
					   scst_dev_resync_params_alloc, scst_dev_resync_params_free),
	.file_path = scst_dev_resync_mgmt_file_path,
	.data = scst_dev_resync_data,
};


static const char *
scst_dev_list_name(void *arg)
{
	return spdk_sprintf_alloc("devices");
}

static char *
scst_dev_list_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEVICES);
}

static struct sto_readdir_req_params_constructor dev_list_constructor = {
	.name = scst_dev_list_name,
	.dirpath = scst_dev_list_dirpath,
};

struct scst_dgrp_params {
	char *name;
};

static void *
scst_dgrp_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dgrp_params));
}

static void
scst_dgrp_params_free(void *arg)
{
	struct scst_dgrp_params *params = arg;
	free(params->name);
	free(params);
}

static const struct spdk_json_object_decoder scst_dgrp_decoders[] = {
	{"name", offsetof(struct scst_dgrp_params, name), spdk_json_decode_string},
};

static const char *
scst_dgrp_mgmt_file_path(void *arg)
{
	return spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS, SCST_MGMT_IO);
}

static char *
scst_dgrp_add_data(void *arg)
{
	struct scst_dgrp_params *params = arg;
	return spdk_sprintf_alloc("create %s", params->name);
}

static char *
scst_dgrp_del_data(void *arg)
{
	struct scst_dgrp_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->name);
}

static struct sto_write_req_params_constructor dgrp_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_decoders,
					   scst_dgrp_params_alloc, scst_dgrp_params_free),
	.file_path = scst_dgrp_mgmt_file_path,
	.data = scst_dgrp_add_data,
};

static struct sto_write_req_params_constructor dgrp_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_decoders,
					   scst_dgrp_params_alloc, scst_dgrp_params_free),
	.file_path = scst_dgrp_mgmt_file_path,
	.data = scst_dgrp_del_data,
};

static const char *
scst_dgrp_list_name(void *arg)
{
	return spdk_sprintf_alloc("Device Group");
}

static char *
scst_dgrp_list_dirpath(void *arg)
{
	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_DEV_GROUPS);
}

static int
scst_dgrp_list_exclude(const char **exclude_list)
{
	exclude_list[0] = SCST_MGMT_IO;

	return 0;
}

static struct sto_readdir_req_params_constructor dgrp_list_constructor = {
	.name = scst_dgrp_list_name,
	.dirpath = scst_dgrp_list_dirpath,
	.exclude = scst_dgrp_list_exclude,
};

struct scst_dgrp_dev_params {
	char *name;
	char *dev_name;
};

static void *
scst_dgrp_dev_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_dgrp_dev_params));
}

static void
scst_dgrp_dev_params_free(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;

	free(params->name);
	free(params->dev_name);
	free(params);
}

static const struct spdk_json_object_decoder scst_dgrp_dev_decoders[] = {
	{"name", offsetof(struct scst_dgrp_dev_params, name), spdk_json_decode_string},
	{"dev_name", offsetof(struct scst_dgrp_dev_params, dev_name), spdk_json_decode_string},
};

static const char *
scst_dgrp_dev_mgmt_file_path(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->name, "devices", SCST_MGMT_IO);
}

static char *
scst_dgrp_add_dev_data(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;
	return spdk_sprintf_alloc("add %s", params->dev_name);
}

static char *
scst_dgrp_del_dev_data(void *arg)
{
	struct scst_dgrp_dev_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->dev_name);
}

static struct sto_write_req_params_constructor dgrp_add_dev_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_dev_decoders,
					   scst_dgrp_dev_params_alloc, scst_dgrp_dev_params_free),
	.file_path = scst_dgrp_dev_mgmt_file_path,
	.data = scst_dgrp_add_dev_data,
};

static struct sto_write_req_params_constructor dgrp_del_dev_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_dgrp_dev_decoders,
					   scst_dgrp_dev_params_alloc, scst_dgrp_dev_params_free),
	.file_path = scst_dgrp_dev_mgmt_file_path,
	.data = scst_dgrp_del_dev_data,
};

struct scst_tgrp_params {
	char *name;
	char *dgrp_name;
};

static void *
scst_tgrp_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_tgrp_params));
}

static void
scst_tgrp_params_free(void *arg)
{
	struct scst_tgrp_params *params = arg;

	free(params->name);
	free(params->dgrp_name);
	free(params);
}

static const struct spdk_json_object_decoder scst_tgrp_decoders[] = {
	{"name", offsetof(struct scst_tgrp_params, name), spdk_json_decode_string},
	{"dgrp_name", offsetof(struct scst_tgrp_params, dgrp_name), spdk_json_decode_string},
};

static const char *
scst_tgrp_mgmt_file_path(void *arg)
{
	struct scst_tgrp_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->dgrp_name, "target_groups", SCST_MGMT_IO);
}

static char *
scst_tgrp_add_data(void *arg)
{
	struct scst_tgrp_params *params = arg;
	return spdk_sprintf_alloc("add %s", params->name);
}

static char *
scst_tgrp_del_data(void *arg)
{
	struct scst_tgrp_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->name);
}

static struct sto_write_req_params_constructor tgrp_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_decoders,
					   scst_tgrp_params_alloc, scst_tgrp_params_free),
	.file_path = scst_tgrp_mgmt_file_path,
	.data = scst_tgrp_add_data,
};

static struct sto_write_req_params_constructor tgrp_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_decoders,
					   scst_tgrp_params_alloc, scst_tgrp_params_free),
	.file_path = scst_tgrp_mgmt_file_path,
	.data = scst_tgrp_del_data,
};

struct scst_tgrp_list_params {
	char *dgrp;
};

static void *
scst_tgrp_list_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_tgrp_list_params));
}

static void
scst_tgrp_list_params_free(void *arg)
{
	struct scst_tgrp_list_params *params = arg;

	free(params->dgrp);
	free(params);
}

static const struct spdk_json_object_decoder scst_tgrp_list_decoders[] = {
	{"dgrp", offsetof(struct scst_tgrp_list_params, dgrp), spdk_json_decode_string},
};

static const char *
scst_tgrp_list_name(void *arg)
{
	return spdk_sprintf_alloc("Target Groups");
}

static char *
scst_tgrp_list_dirpath(void *arg)
{
	struct scst_tgrp_list_params *params = arg;

	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->dgrp, "target_groups");
}

static int
scst_tgrp_list_exclude(const char **exclude_list)
{
	exclude_list[0] = SCST_MGMT_IO;

	return 0;
}

static struct sto_readdir_req_params_constructor tgrp_list_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_list_decoders,
					   scst_tgrp_list_params_alloc, scst_tgrp_list_params_free),
	.name = scst_tgrp_list_name,
	.dirpath = scst_tgrp_list_dirpath,
	.exclude = scst_tgrp_list_exclude,
};

struct scst_tgrp_tgt_params {
	char *tgt_name;
	char *dgrp_name;
	char *tgrp_name;
};

static void *
scst_tgrp_tgt_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_tgrp_tgt_params));
}

static void
scst_tgrp_tgt_params_free(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;

	free(params->tgt_name);
	free(params->dgrp_name);
	free(params->tgrp_name);
	free(params);
}

static const struct spdk_json_object_decoder scst_tgrp_tgt_decoders[] = {
	{"tgt_name", offsetof(struct scst_tgrp_tgt_params, tgt_name), spdk_json_decode_string},
	{"dgrp_name", offsetof(struct scst_tgrp_tgt_params, dgrp_name), spdk_json_decode_string},
	{"tgrp_name", offsetof(struct scst_tgrp_tgt_params, tgrp_name), spdk_json_decode_string},
};

static const char *
scst_tgrp_tgt_mgmt_file_path(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_DEV_GROUPS,
				  params->dgrp_name, "target_groups",
				  params->tgrp_name, SCST_MGMT_IO);
}

static char *
scst_tgrp_add_tgt_data(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;
	return spdk_sprintf_alloc("add %s", params->tgt_name);
}

static char *
scst_tgrp_del_tgt_data(void *arg)
{
	struct scst_tgrp_tgt_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->tgt_name);
}

static struct sto_write_req_params_constructor tgrp_add_tgt_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_tgt_decoders,
					   scst_tgrp_tgt_params_alloc, scst_tgrp_tgt_params_free),
	.file_path = scst_tgrp_tgt_mgmt_file_path,
	.data = scst_tgrp_add_tgt_data,
};

static struct sto_write_req_params_constructor tgrp_del_tgt_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_tgrp_tgt_decoders,
					   scst_tgrp_tgt_params_alloc, scst_tgrp_tgt_params_free),
	.file_path = scst_tgrp_tgt_mgmt_file_path,
	.data = scst_tgrp_del_tgt_data,
};


struct scst_target_params {
	char *target;
	char *driver;
};

static void *
scst_target_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_target_params));
}

static void
scst_target_params_free(void *arg)
{
	struct scst_target_params *params = arg;

	free(params->target);
	free(params->driver);
	free(params);
}

static const struct spdk_json_object_decoder scst_target_decoders[] = {
	{"target", offsetof(struct scst_target_params, target), spdk_json_decode_string},
	{"driver", offsetof(struct scst_target_params, driver), spdk_json_decode_string},
};

static const char *
scst_target_mgmt_file_path(void *arg)
{
	struct scst_target_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  params->driver, SCST_MGMT_IO);
}

static char *
scst_target_add_data(void *arg)
{
	struct scst_target_params *params = arg;
	return spdk_sprintf_alloc("add_target %s", params->target);
}

static char *
scst_target_del_data(void *arg)
{
	struct scst_target_params *params = arg;
	return spdk_sprintf_alloc("del_target %s", params->target);
}

static struct sto_write_req_params_constructor target_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.file_path = scst_target_mgmt_file_path,
	.data = scst_target_add_data,
};

static struct sto_write_req_params_constructor target_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.file_path = scst_target_mgmt_file_path,
	.data = scst_target_del_data,
};

struct scst_target_list_params {
	char *driver;
};

static void *
scst_target_list_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_target_list_params));
}

static void
scst_target_list_params_free(void *arg)
{
	struct scst_target_list_params *params = arg;

	free(params->driver);
	free(params);
}

static const struct spdk_json_object_decoder scst_target_list_decoders[] = {
	{"driver", offsetof(struct scst_target_list_params, driver), spdk_json_decode_string, true},
};

static char *
scst_target_list_dirpath(void *arg)
{
	struct scst_target_list_params *params = arg;

	if (params) {
		return spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_TARGETS, params->driver);
	}

	return spdk_sprintf_alloc("%s/%s", SCST_ROOT, SCST_TARGETS);
}

static uint32_t
scst_target_list_depth(void *arg)
{
	struct scst_target_list_params *params = arg;

	return params ? 1 : 2;
}

static bool
scst_target_list_only_dirs(void *arg)
{
	return true;
}

static struct sto_tree_req_params_constructor target_list_constructor = {
	.decoder = STO_DECODER_INITIALIZER_EMPTY(scst_target_list_decoders,
						 scst_target_list_params_alloc, scst_target_list_params_free),
	.dirpath = scst_target_list_dirpath,
	.depth = scst_target_list_depth,
	.only_dirs = scst_target_list_only_dirs,
};

static const char *
scst_target_enable_file_path(void *arg)
{
	struct scst_target_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  params->driver, params->target, "enabled");
}

static char *
scst_target_enable_data(void *arg)
{
	return spdk_sprintf_alloc("1");
}

static char *
scst_target_disable_data(void *arg)
{
	return spdk_sprintf_alloc("0");
}

static struct sto_write_req_params_constructor target_enable_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.file_path = scst_target_enable_file_path,
	.data = scst_target_enable_data,
};

static struct sto_write_req_params_constructor target_disable_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_target_decoders,
					   scst_target_params_alloc, scst_target_params_free),
	.file_path = scst_target_enable_file_path,
	.data = scst_target_disable_data,
};

struct scst_group_params {
	char *group;
	char *driver;
	char *target;
};

static void *
scst_group_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_group_params));
}

static void
scst_group_params_free(void *arg)
{
	struct scst_group_params *params = arg;

	free(params->group);
	free(params->driver);
	free(params->target);
	free(params);
}

static const struct spdk_json_object_decoder scst_group_decoders[] = {
	{"group", offsetof(struct scst_group_params, group), spdk_json_decode_string},
	{"driver", offsetof(struct scst_group_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_group_params, target), spdk_json_decode_string},
};

static const char *
scst_group_mgmt_file_path(void *arg)
{
	struct scst_group_params *params = arg;
	return spdk_sprintf_alloc("%s/%s/%s/%s/%s/%s", SCST_ROOT, SCST_TARGETS,
				  params->driver, params->target, "ini_groups", SCST_MGMT_IO);
}

static char *
scst_group_add_data(void *arg)
{
	struct scst_group_params *params = arg;
	return spdk_sprintf_alloc("create %s", params->group);
}

static char *
scst_group_del_data(void *arg)
{
	struct scst_group_params *params = arg;
	return spdk_sprintf_alloc("del %s", params->group);
}

static struct sto_write_req_params_constructor group_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_group_decoders,
					   scst_group_params_alloc, scst_group_params_free),
	.file_path = scst_group_mgmt_file_path,
	.data = scst_group_add_data,
};

static struct sto_write_req_params_constructor group_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_group_decoders,
					   scst_group_params_alloc, scst_group_params_free),
	.file_path = scst_group_mgmt_file_path,
	.data = scst_group_del_data,
};

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
	struct scst_attr_name_list attr_list;
};

static void *
scst_lun_add_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_lun_add_params));
}

static void
scst_lun_add_params_free(void *arg)
{
	struct scst_lun_add_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->device);
	free(params->group);
	scst_attr_list_free(&params->attr_list);
	free(params);
}

static const struct spdk_json_object_decoder scst_lun_add_decoders[] = {
	{"lun", offsetof(struct scst_lun_add_params, lun), spdk_json_decode_int32},
	{"driver", offsetof(struct scst_lun_add_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_add_params, target), spdk_json_decode_string},
	{"device", offsetof(struct scst_lun_add_params, device), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_add_params, group), spdk_json_decode_string, true},
	{"attributes", offsetof(struct scst_lun_add_params, attr_list), scst_attr_list_decode, true},
};

static const char *
scst_lun_add_mgmt_file_path(void *arg)
{
	struct scst_lun_add_params *params = arg;

	return scst_lun_mgmt_file_constructor(params->driver, params->target, params->group);
}

static char *
scst_lun_add_data(void *arg)
{
	struct scst_lun_add_params *params = arg;
	char *data;
	int rc;

	data = spdk_sprintf_alloc("add %s %d", params->device, params->lun);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return NULL;
	}

	rc = scst_attr_list_fill_data(&params->attr_list, &data);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to fill scst attrs\n");
		free(data);
		return NULL;
	}

	return data;
}

static struct sto_write_req_params_constructor lun_add_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_lun_add_decoders,
					   scst_lun_add_params_alloc, scst_lun_add_params_free),
	.file_path = scst_lun_add_mgmt_file_path,
	.data = scst_lun_add_data,
};

struct scst_lun_del_params {
	int lun;
	char *driver;
	char *target;
	char *group;
};

static void *
scst_lun_del_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_lun_del_params));
}

static void
scst_lun_del_params_free(void *arg)
{
	struct scst_lun_del_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->group);
	free(params);
}

static const struct spdk_json_object_decoder scst_lun_del_decoders[] = {
	{"lun", offsetof(struct scst_lun_add_params, lun), spdk_json_decode_int32},
	{"driver", offsetof(struct scst_lun_del_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_del_params, target), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_del_params, group), spdk_json_decode_string, true},
};

static const char *
scst_lun_del_mgmt_file_path(void *arg)
{
	struct scst_lun_del_params *params = arg;

	return scst_lun_mgmt_file_constructor(params->driver, params->target, params->group);
}

static char *
scst_lun_del_data(void *arg)
{
	struct scst_lun_del_params *params = arg;

	return spdk_sprintf_alloc("del %d", params->lun);
}

static struct sto_write_req_params_constructor lun_del_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_lun_del_decoders,
					   scst_lun_del_params_alloc, scst_lun_del_params_free),
	.file_path = scst_lun_del_mgmt_file_path,
	.data = scst_lun_del_data,
};

static char *
scst_lun_replace_data(void *arg)
{
	struct scst_lun_add_params *params = arg;
	char *data;
	int rc;

	data = spdk_sprintf_alloc("replace %s %d", params->device, params->lun);
	if (spdk_unlikely(!data)) {
		SPDK_ERRLOG("Failed to alloc memory for data\n");
		return NULL;
	}

	rc = scst_attr_list_fill_data(&params->attr_list, &data);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to fill scst attrs\n");
		free(data);
		return NULL;
	}

	return data;
}

static struct sto_write_req_params_constructor lun_replace_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_lun_add_decoders,
					   scst_lun_add_params_alloc, scst_lun_add_params_free),
	.file_path = scst_lun_add_mgmt_file_path,
	.data = scst_lun_replace_data,
};

struct scst_lun_clear_params {
	char *driver;
	char *target;
	char *group;
};

static void *
scst_lun_clear_params_alloc(void)
{
	return calloc(1, sizeof(struct scst_lun_clear_params));
}

static void
scst_lun_clear_params_free(void *arg)
{
	struct scst_lun_clear_params *params = arg;

	free(params->driver);
	free(params->target);
	free(params->group);
	free(params);
}

static const struct spdk_json_object_decoder scst_lun_clear_decoders[] = {
	{"driver", offsetof(struct scst_lun_clear_params, driver), spdk_json_decode_string},
	{"target", offsetof(struct scst_lun_clear_params, target), spdk_json_decode_string},
	{"group", offsetof(struct scst_lun_clear_params, group), spdk_json_decode_string, true},
};

static const char *
scst_lun_clear_mgmt_file_path(void *arg)
{
	struct scst_lun_clear_params *params = arg;

	return scst_lun_mgmt_file_constructor(params->driver, params->target, params->group);
}

static char *
scst_lun_clear_data(void *arg)
{
	return spdk_sprintf_alloc("clear");
}

static struct sto_write_req_params_constructor lun_clear_constructor = {
	.decoder = STO_DECODER_INITIALIZER(scst_lun_clear_decoders,
					   scst_lun_clear_params_alloc, scst_lun_clear_params_free),
	.file_path = scst_lun_clear_mgmt_file_path,
	.data = scst_lun_clear_data,
};

static const struct sto_cdbops scst_op_table[] = {
	{
		.name = "config_write",
		.req_constructor = sto_tree_req_constructor,
		.req_ops = &sto_tree_req_ops,
		.params_constructor = &config_write_constructor,
	},
	{
		.name = "handler_list",
		.req_constructor = sto_readdir_req_constructor,
		.req_ops = &sto_readdir_req_ops,
		.params_constructor = &handler_list_constructor,
	},
	{
		.name = "driver_list",
		.req_constructor = sto_readdir_req_constructor,
		.req_ops = &sto_readdir_req_ops,
		.params_constructor = &driver_list_constructor,
	},
	{
		.name = "dev_open",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dev_open_constructor,
	},
	{
		.name = "dev_close",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dev_close_constructor,
	},
	{
		.name = "dev_resync",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dev_resync_constructor,
	},
	{
		.name = "dev_list",
		.req_constructor = sto_readdir_req_constructor,
		.req_ops = &sto_readdir_req_ops,
		.params_constructor = &dev_list_constructor,
	},
	{
		.name = "dgrp_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dgrp_add_constructor,
	},
	{
		.name = "dgrp_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dgrp_del_constructor,
	},
	{
		.name = "dgrp_list",
		.req_constructor = sto_readdir_req_constructor,
		.req_ops = &sto_readdir_req_ops,
		.params_constructor = &dgrp_list_constructor,
	},
	{
		.name = "dgrp_add_dev",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dgrp_add_dev_constructor,
	},
	{
		.name = "dgrp_del_dev",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &dgrp_del_dev_constructor,
	},
	{
		.name = "tgrp_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &tgrp_add_constructor,
	},
	{
		.name = "tgrp_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &tgrp_del_constructor,
	},
	{
		.name = "tgrp_list",
		.req_constructor = sto_readdir_req_constructor,
		.req_ops = &sto_readdir_req_ops,
		.params_constructor = &tgrp_list_constructor,
	},
	{
		.name = "tgrp_add_tgt",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &tgrp_add_tgt_constructor,
	},
	{
		.name = "tgrp_del_tgt",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &tgrp_del_tgt_constructor,
	},
	{
		.name = "target_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &target_add_constructor,
	},
	{
		.name = "target_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &target_del_constructor,
	},
	{
		.name = "target_list",
		.req_constructor = sto_tree_req_constructor,
		.req_ops = &sto_tree_req_ops,
		.params_constructor = &target_list_constructor,
	},
	{
		.name = "target_enable",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &target_enable_constructor,
	},
	{
		.name = "target_disable",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &target_disable_constructor,
	},
	{
		.name = "group_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &group_add_constructor,
	},
	{
		.name = "group_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &group_del_constructor,
	},
	{
		.name = "lun_add",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &lun_add_constructor,
	},
	{
		.name = "lun_del",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &lun_del_constructor,
	},
	{
		.name = "lun_replace",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &lun_replace_constructor,
	},
	{
		.name = "lun_clear",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &lun_clear_constructor,
	},
};

#define SCST_OP_TBL_SIZE	(SPDK_COUNTOF(scst_op_table))

static const struct sto_cdbops *
scst_find_cdbops(const char *op_name)
{
	int i;

	for (i = 0; i < SCST_OP_TBL_SIZE; i++) {
		const struct sto_cdbops *op = &scst_op_table[i];

		if (!strcmp(op_name, op->name)) {
			return op;
		}
	}

	return NULL;
}

static struct sto_subsystem g_scst_subsystem = {
	.name = "scst",
	.find_cdbops = scst_find_cdbops,
};

STO_SUBSYSTEM_REGISTER(g_scst_subsystem);
