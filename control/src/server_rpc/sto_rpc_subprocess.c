#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_string_fns.h>

#include "sto_client.h"
#include "sto_rpc_subprocess.h"

struct sto_rpc_subprocess_info {
	int returncode;
	char **output;
};

static int
sto_rpc_subprocess_output_decode(const struct spdk_json_val *val, void *out)
{
	char **output = *(char ***) out;

	return spdk_json_decode_string(val, output);
}

static const struct spdk_json_object_decoder sto_rpc_subprocess_info_decoders[] = {
	{"returncode", offsetof(struct sto_rpc_subprocess_info, returncode), spdk_json_decode_int32},
	{"output", offsetof(struct sto_rpc_subprocess_info, output), sto_rpc_subprocess_output_decode, true},
};

struct sto_rpc_subprocess_params {
	int numargs;
	const char *const *argv;
	bool capture_output;
};

struct sto_rpc_subprocess_cmd {
	void *priv;
	sto_rpc_subprocess_done_t done;

	char **output;
};

static struct sto_rpc_subprocess_cmd *
sto_rpc_subprocess_cmd_alloc(void)
{
	struct sto_rpc_subprocess_cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for subprocess\n");
		return NULL;
	}

	return cmd;
}

static void
sto_rpc_subprocess_cmd_init_cb(struct sto_rpc_subprocess_cmd *cmd,
			       sto_rpc_subprocess_done_t done, void *priv)
{
	cmd->done = done;
	cmd->priv = priv;
}

static void
sto_rpc_subprocess_cmd_free(struct sto_rpc_subprocess_cmd *cmd)
{
	free(cmd);
}

static void
sto_rpc_subprocess_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct sto_rpc_subprocess_cmd *cmd = priv;
	struct sto_rpc_subprocess_info info = {
		.output = cmd->output,
	};

	if (spdk_unlikely(rc)) {
		goto out;
	}

	if (spdk_json_decode_object(resp->result, sto_rpc_subprocess_info_decoders,
				    SPDK_COUNTOF(sto_rpc_subprocess_info_decoders), &info)) {
		SPDK_ERRLOG("Failed to decode response for subprocess\n");
		rc = -ENOMEM;
		goto out;
	}

	rc = info.returncode;

out:
	cmd->done(cmd->priv, rc);
	sto_rpc_subprocess_cmd_free(cmd);
}

static void
sto_rpc_subprocess_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_subprocess_params *params = priv;
	int i;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "cmd");
	for (i = 0; i < params->numargs; i++) {
		spdk_json_write_string(w, params->argv[i]);
	}
	spdk_json_write_array_end(w);

	spdk_json_write_named_bool(w, "capture_output", params->capture_output);

	spdk_json_write_object_end(w);
}

static int
sto_rpc_subprocess_cmd_run(struct sto_rpc_subprocess_cmd *cmd, struct sto_rpc_subprocess_params *params)
{
	struct sto_client_args args = {
		.priv = cmd,
		.response_handler = sto_rpc_subprocess_resp_handler,
	};

	return sto_client_send("subprocess", params, sto_rpc_subprocess_info_json, &args);
}

int
sto_rpc_subprocess(const char *const *argv, int numargs,
		   struct sto_rpc_subprocess_args *args)
{
	struct sto_rpc_subprocess_cmd *cmd;
	struct sto_rpc_subprocess_params params = {
		.numargs = numargs,
		.argv = argv,
		.capture_output = args->output != NULL,
	};
	int rc = 0;

	assert(numargs);

	cmd = sto_rpc_subprocess_cmd_alloc();
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to create subprocess\n");
		return -ENOMEM;
	}

	cmd->output = args->output;

	sto_rpc_subprocess_cmd_init_cb(cmd, args->done, args->priv);

	rc = sto_rpc_subprocess_cmd_run(cmd, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run subprocess, rc=%d\n", rc);
		goto free_cmd;
	}

	return 0;

free_cmd:
	sto_rpc_subprocess_cmd_free(cmd);

	return rc;
}

int
sto_rpc_subprocess_fmt(const char *fmt, struct sto_rpc_subprocess_args *args, ...)
{
	va_list fmt_args;
#define STO_SUBPROCESS_MAX_ARGS 128
	char *argv_s, *argv[STO_SUBPROCESS_MAX_ARGS] = {};
	int ret, rc = 0;

	assert(fmt && fmt[0] != '\0');

	va_start(fmt_args, args);
	argv_s = spdk_vsprintf_alloc(fmt, fmt_args);
	va_end(fmt_args);

	if (spdk_unlikely(!argv_s)) {
		SPDK_ERRLOG("Failed to alloc format string for subprocess arguments\n");
		return -ENOMEM;
	}

	ret = rte_strsplit(argv_s, strlen(argv_s), argv, SPDK_COUNTOF(argv), ' ');
	if (spdk_unlikely(ret <= 0)) {
		SPDK_ERRLOG("Failed to split subprocess arguments string, ret=%d\n", ret);
		rc = -EINVAL;
		goto out;
	}

	rc = sto_rpc_subprocess((const char * const *) argv, ret, args);

out:
	free(argv_s);

	return rc;
}
