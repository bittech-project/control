#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_lib.h"
#include "sto_subsystem.h"

struct sys_writefile_params {
	char *filepath;
	char *data;
};

static void
sys_writefile_params_deinit(void *arg)
{
	struct sys_writefile_params *params = arg;

	free(params->filepath);
	free(params->data);
}

static const struct spdk_json_object_decoder sys_writefile_decoders[] = {
	{"filepath", offsetof(struct sys_writefile_params, filepath), spdk_json_decode_string},
	{"data", offsetof(struct sys_writefile_params, data), spdk_json_decode_string},
};

const struct sto_ops_decoder sys_writefile_decoder =
	STO_OPS_DECODER_INITIALIZER(sys_writefile_decoders,
				    sizeof(struct sys_writefile_params),
				    sys_writefile_params_deinit);

static int
sys_writefile_constructor(void *arg1, void *arg2)
{
	struct sto_write_req_params *req_params = arg1;
	struct sys_writefile_params *ops_params = arg2;

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

static void
sys_readfile_params_deinit(void *arg)
{
	struct sys_readfile_params *params = arg;
	free(params->filepath);
}

static const struct spdk_json_object_decoder sys_readfile_decoders[] = {
	{"filepath", offsetof(struct sys_readfile_params, filepath), spdk_json_decode_string},
	{"size", offsetof(struct sys_readfile_params, size), spdk_json_decode_uint32, true},
};

const struct sto_ops_decoder sys_readfile_decoder =
	STO_OPS_DECODER_INITIALIZER(sys_readfile_decoders,
				    sizeof(struct sys_readfile_params),
				    sys_readfile_params_deinit);

static int
sys_readfile_constructor(void *arg1, void *arg2)
{
	struct sto_read_req_params *req_params = arg1;
	struct sys_readfile_params *ops_params = arg2;

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

static void
sys_readlink_params_deinit(void *arg)
{
	struct sys_readlink_params *params = arg;

	free(params->filepath);
}

static const struct spdk_json_object_decoder sys_readlink_decoders[] = {
	{"filepath", offsetof(struct sys_readlink_params, filepath), spdk_json_decode_string},
};

const struct sto_ops_decoder sys_readlink_decoder =
	STO_OPS_DECODER_INITIALIZER(sys_readlink_decoders,
				    sizeof(struct sys_readlink_params),
				    sys_readlink_params_deinit);

static int
sys_readlink_constructor(void *arg1, void *arg2)
{
	struct sto_readlink_req_params *req_params = arg1;
	struct sys_readlink_params *ops_params = arg2;

	req_params->file = strdup(ops_params->filepath);
	if (spdk_unlikely(!req_params->file)) {
		return -ENOMEM;
	}

	return 0;
}

struct sys_readdir_params {
	char *dirpath;
};

static void
sys_readdir_params_deinit(void *arg)
{
	struct sys_readdir_params *params = arg;
	free(params->dirpath);
}

static const struct spdk_json_object_decoder sys_readdir_decoders[] = {
	{"dirpath", offsetof(struct sys_readdir_params, dirpath), spdk_json_decode_string},
};

const struct sto_ops_decoder sys_readdir_decoder =
	STO_OPS_DECODER_INITIALIZER(sys_readdir_decoders,
				    sizeof(struct sys_readdir_params),
				    sys_readdir_params_deinit);

static int
sys_readdir_constructor(void *arg1, void *arg2)
{
	struct sto_readdir_req_params *req_params = arg1;
	struct sys_readdir_params *ops_params = arg2;

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
		.decoder = &sys_writefile_decoder,
		.req_properties = &sto_write_req_properties,
		.req_params_constructor = &sys_writefile_constructor,
	},
	{
		.name = "readfile",
		.decoder = &sys_readfile_decoder,
		.req_properties = &sto_read_req_properties,
		.req_params_constructor = &sys_readfile_constructor,
	},
	{
		.name = "readlink",
		.decoder = &sys_readlink_decoder,
		.req_properties = &sto_readlink_req_properties,
		.req_params_constructor = &sys_readlink_constructor,
	},
	{
		.name = "readdir",
		.decoder = &sys_readdir_decoder,
		.req_properties = &sto_readdir_req_properties,
		.req_params_constructor = &sys_readdir_constructor,
	},
};

static const struct sto_op_table sys_op_table = STO_OP_TABLE_INITIALIZER(sys_ops);

static struct sto_subsystem g_sys_subsystem = STO_SUBSYSTEM_INITIALIZER("sys", &sys_op_table);
STO_SUBSYSTEM_REGISTER(g_sys_subsystem);
