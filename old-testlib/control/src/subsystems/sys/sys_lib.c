#include <spdk/stdinc.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_generic_req.h"
#include "sto_subsystem.h"
#include "sto_lib.h"

struct sys_writefile_params {
	char *filepath;
	char *data;
};

static const struct sto_ops_param_dsc sys_writefile_params_descriptors[] = {
	STO_OPS_PARAM_STR(filepath, struct sys_writefile_params, "filepath desc"),
	STO_OPS_PARAM_STR(data, struct sys_writefile_params, "data desc"),
};

static const struct sto_ops_params_properties sys_writefile_params_properties =
	STO_OPS_PARAMS_INITIALIZER(sys_writefile_params_descriptors, struct sys_writefile_params);

static int
sys_writefile_constructor(void *arg1, const void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	const struct sys_writefile_params *ops_params = arg2;

	req_params->file = strdup(ops_params->filepath);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->data = strdup(ops_params->data);
	if (spdk_unlikely(!req_params->data)) {
		return -ENOMEM;
	}

	return 0;
}

struct sys_readfile_params {
	char *filepath;
	uint32_t size;
};

static const struct sto_ops_param_dsc sys_readfile_params_descriptors[] = {
	STO_OPS_PARAM_STR(filepath, struct sys_readfile_params, "filepath desc"),
	STO_OPS_PARAM_UINT32_OPTIONAL(size, struct sys_readfile_params, "size desc"),
};

static const struct sto_ops_params_properties sys_readfile_params_properties =
	STO_OPS_PARAMS_INITIALIZER(sys_readfile_params_descriptors, struct sys_readfile_params);

static int
sys_readfile_constructor(void *arg1, const void *arg2)
{
	struct sto_read_req_params *req_params = arg1;
	const struct sys_readfile_params *ops_params = arg2;

	req_params->file = strdup(ops_params->filepath);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	req_params->size = ops_params->size;

	return 0;
}

struct sys_readlink_params {
	char *filepath;
};

static const struct sto_ops_param_dsc sys_readlink_params_descriptors[] = {
	STO_OPS_PARAM_STR(filepath, struct sys_readlink_params, "filepath desc"),
};

static const struct sto_ops_params_properties sys_readlink_params_properties =
	STO_OPS_PARAMS_INITIALIZER(sys_readlink_params_descriptors, struct sys_readlink_params);

static int
sys_readlink_constructor(void *arg1, const void *arg2)
{
	struct sto_readlink_req_params *req_params = arg1;
	const struct sys_readlink_params *ops_params = arg2;

	req_params->file = strdup(ops_params->filepath);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	return 0;
}

struct sys_readdir_params {
	char *dirpath;
};

static const struct sto_ops_param_dsc sys_readdir_params_descriptors[] = {
	STO_OPS_PARAM_STR(dirpath, struct sys_readdir_params, "dirpath desc"),
};

static const struct sto_ops_params_properties sys_readdir_params_properties =
	STO_OPS_PARAMS_INITIALIZER(sys_readdir_params_descriptors, struct sys_readdir_params);

static int
sys_readdir_constructor(void *arg1, const void *arg2)
{
	struct sto_readdir_req_params *req_params = arg1;
	const struct sys_readdir_params *ops_params = arg2;

	req_params->name = spdk_sprintf_alloc("Files");
	if (spdk_unlikely(!req_params->name)) {
		return -ENOMEM;
	}

	req_params->dirpath = strdup(ops_params->dirpath);
	if (spdk_unlikely(!req_params->dirpath)) {
		return -ENOMEM;
	}

	return 0;
}

static const struct sto_ops sys_ops[] = {
	{
		.name = "writefile",
		.params_properties = &sys_writefile_params_properties,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = sys_writefile_constructor,
	},
	{
		.name = "readfile",
		.params_properties = &sys_readfile_params_properties,
		.req_properties = &sto_read_req_properties,
		.req_params_constructor = sys_readfile_constructor,
	},
	{
		.name = "readlink",
		.params_properties = &sys_readlink_params_properties,
		.req_properties = &sto_readlink_req_properties,
		.req_params_constructor = sys_readlink_constructor,
	},
	{
		.name = "readdir",
		.params_properties = &sys_readdir_params_properties,
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = sys_readdir_constructor,
	},
};

static const struct sto_op_table sys_op_table = STO_OP_TABLE_INITIALIZER(sys_ops);

STO_SUBSYSTEM_REGISTER(sys, &sys_op_table, NULL);
