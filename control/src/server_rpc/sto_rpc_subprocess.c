#include "sto_rpc_subprocess.h"

#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>
#include <spdk/json.h>
#include <spdk/jsonrpc.h>
#include <spdk/util.h>

#include "sto_client.h"
#include "sto_async.h"

struct spdk_json_write_ctx;

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
	const char *const *argv;
	bool capture_output;
};

struct sto_rpc_subprocess_cmd {
	void *cb_arg;
	sto_generic_cb cb_fn;

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
			       sto_generic_cb cb_fn, void *cb_arg)
{
	cmd->cb_fn = cb_fn;
	cmd->cb_arg = cb_arg;
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
	cmd->cb_fn(cmd->cb_arg, rc);
	sto_rpc_subprocess_cmd_free(cmd);
}

static void
sto_rpc_subprocess_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_subprocess_params *params = priv;
	int i;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "cmd");
	for (i = 0; params->argv[i] != NULL; i++) {
		spdk_json_write_string(w, params->argv[i]);
	}
	spdk_json_write_array_end(w);

	spdk_json_write_named_bool(w, "capture_output", params->capture_output);

	spdk_json_write_object_end(w);
}

static int
sto_rpc_subprocess_cmd_run(struct sto_rpc_subprocess_cmd *cmd,
			   struct sto_rpc_subprocess_params *params)
{
	struct sto_client_args args = {
		.priv = cmd,
		.response_handler = sto_rpc_subprocess_resp_handler,
	};

	return sto_client_send("subprocess", params, sto_rpc_subprocess_info_json, &args);
}

void
sto_rpc_subprocess(const char *const *argv, sto_generic_cb cb_fn, void *cb_arg, char **output)
{
	struct sto_rpc_subprocess_cmd *cmd;
	struct sto_rpc_subprocess_params params = {
		.argv = argv,
		.capture_output = output != NULL,
	};
	int rc = 0;

	assert(argv[0] != NULL);

	cmd = sto_rpc_subprocess_cmd_alloc();
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to create subprocess\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	cmd->output = output;

	sto_rpc_subprocess_cmd_init_cb(cmd, cb_fn, cb_arg);

	rc = sto_rpc_subprocess_cmd_run(cmd, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run subprocess, rc=%d\n", rc);
		goto free_cmd;
	}

	return;

free_cmd:
	sto_rpc_subprocess_cmd_free(cmd);
	cb_fn(cb_arg, rc);

	return;
}

void
sto_rpc_subprocess_fmt(const char *fmt, sto_generic_cb cb_fn, void *cb_arg, char **output, ...)
{
	va_list fmt_args;
	char *argv_s;
	const char * const *argv;

	assert(fmt && fmt[0] != '\0');

	va_start(fmt_args, output);
	argv_s = spdk_vsprintf_alloc(fmt, fmt_args);
	va_end(fmt_args);

	if (spdk_unlikely(!argv_s)) {
		SPDK_ERRLOG("Failed to alloc format string for subprocess arguments\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	argv = (const char * const *) spdk_strarray_from_string(argv_s, " ");

	free(argv_s);

	if (spdk_unlikely(!argv)) {
		SPDK_ERRLOG("Failed to split subprocess arguments string\n");
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	sto_rpc_subprocess((const char * const *) argv, cb_fn, cb_arg, output);

	spdk_strarray_free((char **) argv);

	return;
}
