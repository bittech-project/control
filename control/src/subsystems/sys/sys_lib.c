#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_lib.h"

struct sys_writefile_params {
	char *filepath;
	char *data;
};

static void *
sys_writefile_params_alloc(void)
{
	return calloc(1, sizeof(struct sys_writefile_params));
}

static void
sys_writefile_params_free(void *arg)
{
	struct sys_writefile_params *params = arg;

	free(params->filepath);
	free(params->data);
	free(params);
}

static const struct spdk_json_object_decoder sys_writefile_decoders[] = {
	{"path", offsetof(struct sys_writefile_params, filepath), spdk_json_decode_string},
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
					   sys_writefile_params_alloc, sys_writefile_params_free),
	.file_path = sys_writefile_path,
	.data = sys_writefile_data,
};

struct sys_readdir_params {
	char *dirpath;
};

static void *
sys_readdir_params_alloc(void)
{
	return calloc(1, sizeof(struct sys_readdir_params));
}

static void
sys_readdir_params_free(void *arg)
{
	struct sys_readdir_params *params = arg;

	free(params->dirpath);
	free(params);
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
					   sys_readdir_params_alloc, sys_readdir_params_free),
	.name = sys_readdir_name,
	.dirpath = sys_readdir_dirpath,
};

static const struct sto_cdbops sys_op_table[] = {
	{
		.name = "writefile",
		.req_constructor = sto_write_req_constructor,
		.req_ops = &sto_write_req_ops,
		.params_constructor = &writefile_constructor,
	},
	{
		.name = "readdir",
		.req_constructor = sto_readdir_req_constructor,
		.req_ops = &sto_readdir_req_ops,
		.params_constructor = &readdir_constructor,
	},
};

#define SYS_OP_TBL_SIZE	(SPDK_COUNTOF(sys_op_table))

static const struct sto_cdbops *
sys_find_cdbops(const char *op_name)
{
	int i;

	for (i = 0; i < SYS_OP_TBL_SIZE; i++) {
		const struct sto_cdbops *op = &sys_op_table[i];

		if (!strcmp(op_name, op->name)) {
			return op;
		}
	}

	return NULL;
}

static struct sto_subsystem g_sys_subsystem = {
	.name = "sys",
	.find_cdbops = sys_find_cdbops,
};

STO_SUBSYSTEM_REGISTER(g_sys_subsystem);
