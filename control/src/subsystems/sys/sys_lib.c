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

static const char *
sys_writefile_path(void *arg)
{
	struct sys_writefile_params *params = arg;

	return spdk_sprintf_alloc("%s", params->filepath);
}

static char *
sys_writefile_data(void *arg)
{
	struct sys_writefile_params *params = arg;

	return spdk_sprintf_alloc("%s", params->data);
}

static struct sto_write_req_params_constructor writefile_constructor = {
	.decoder = STO_DECODER_INITIALIZER(sys_writefile_decoders,
					   sizeof(struct sys_writefile_params),
					   sys_writefile_params_deinit),
	.file_path = sys_writefile_path,
	.data = sys_writefile_data,
};

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

static const char *
sys_readfile_path(void *arg)
{
	struct sys_readfile_params *params = arg;

	return spdk_sprintf_alloc("%s", params->filepath);
}

static uint32_t
sys_readfile_size(void *arg)
{
	struct sys_readfile_params *params = arg;

	return params->size;
}

static struct sto_read_req_params_constructor readfile_constructor = {
	.decoder = STO_DECODER_INITIALIZER(sys_readfile_decoders,
					   sizeof(struct sys_readfile_params),
					   sys_readfile_params_deinit),
	.file_path = sys_readfile_path,
	.size = sys_readfile_size,
};

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

static const char *
sys_readlink_path(void *arg)
{
	struct sys_readlink_params *params = arg;

	return spdk_sprintf_alloc("%s", params->filepath);
}

static struct sto_readlink_req_params_constructor readlink_constructor = {
	.decoder = STO_DECODER_INITIALIZER(sys_readlink_decoders,
					   sizeof(struct sys_readlink_params),
					   sys_readlink_params_deinit),
	.file_path = sys_readlink_path,
};

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

static const char *
sys_readdir_name(void *arg)
{
	return spdk_sprintf_alloc("Files");
}

static char *
sys_readdir_dirpath(void *arg)
{
	struct sys_readdir_params *params = arg;

	return spdk_sprintf_alloc("%s", params->dirpath);
}

static struct sto_readdir_req_params_constructor readdir_constructor = {
	.decoder = STO_DECODER_INITIALIZER(sys_readdir_decoders,
					   sizeof(struct sys_readdir_params),
					   sys_readdir_params_deinit),
	.name = sys_readdir_name,
	.dirpath = sys_readdir_dirpath,
};

static const struct sto_ops sys_ops[] = {
	{
		.name = "writefile",
		.params_constructor = &writefile_constructor,
		.req_properties = &sto_write_req_properties,
	},
	{
		.name = "readfile",
		.params_constructor = &readfile_constructor,
		.req_properties = &sto_read_req_properties,
	},
	{
		.name = "readlink",
		.params_constructor = &readlink_constructor,
		.req_properties = &sto_readlink_req_properties,
	},
	{
		.name = "readdir",
		.params_constructor = &readdir_constructor,
		.req_properties = &sto_readdir_req_properties,
	},
};

static const struct sto_op_table sys_op_table = STO_OP_TABLE_INITIALIZER(sys_ops);

static struct sto_subsystem g_sys_subsystem = STO_SUBSYSTEM_INITIALIZER("sys", &sys_op_table);
STO_SUBSYSTEM_REGISTER(g_sys_subsystem);
