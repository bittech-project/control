#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_client.h"
#include "sto_rpc_subprocess.h"

struct sto_rpc_subprocess_info {
	int returncode;
	char *output;
};

static void
sto_rpc_subprocess_info_free(struct sto_rpc_subprocess_info *info)
{
	free(info->output);
}

static const struct spdk_json_object_decoder sto_rpc_subprocess_info_decoders[] = {
	{"returncode", offsetof(struct sto_rpc_subprocess_info, returncode), spdk_json_decode_int32},
	{"output", offsetof(struct sto_rpc_subprocess_info, output), spdk_json_decode_string},
};

struct sto_rpc_subprocess_cmd *
sto_rpc_subprocess_cmd_alloc(const char *const argv[], int numargs, bool capture_output)
{
	struct sto_rpc_subprocess_cmd *cmd;
	unsigned int data_len;
	int i;

	if (spdk_unlikely(!numargs)) {
		SPDK_ERRLOG("Too few arguments\n");
		return NULL;
	}

	/* Count the number of bytes for the 'numargs' arguments to be allocated */
	data_len = numargs * sizeof(char *);

	cmd = rte_zmalloc(NULL, sizeof(*cmd) + data_len, 0);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for subprocess\n");
		return NULL;
	}

	cmd->numargs = numargs;
	cmd->capture_output = capture_output;

	for (i = 0; i < cmd->numargs; i++) {
		cmd->args[i] = argv[i];
	}

	return cmd;
}

void
sto_rpc_subprocess_cmd_init_cb(struct sto_rpc_subprocess_cmd *cmd,
			       sto_rpc_subprocess_done_t done, void *priv)
{
	cmd->done = done;
	cmd->priv = priv;
}

void
sto_rpc_subprocess_cmd_free(struct sto_rpc_subprocess_cmd *cmd)
{
	free(cmd->output);
	rte_free(cmd);
}

static void
sto_rpc_subprocess_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct sto_rpc_subprocess_cmd *cmd = priv;
	struct sto_rpc_subprocess_info info = {};

	if (spdk_unlikely(rc)) {
		cmd->returncode = rc;
		goto out;
	}

	if (spdk_json_decode_object(resp->result, sto_rpc_subprocess_info_decoders,
				    SPDK_COUNTOF(sto_rpc_subprocess_info_decoders), &info)) {
		SPDK_ERRLOG("Failed to decode response for subprocess\n");
		cmd->returncode = -ENOMEM;
		goto out;
	}

	cmd->returncode = info.returncode;

	if (cmd->capture_output) {
		cmd->output = strdup(info.output);
		if (spdk_unlikely(!cmd->output)) {
			SPDK_ERRLOG("Failed copy output for subprocess\n");
			cmd->returncode = -ENOMEM;
			goto free_params;
		}
	}

free_params:
	sto_rpc_subprocess_info_free(&info);

out:
	cmd->done(cmd);
}

static void
sto_rpc_subprocess_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_subprocess_cmd *cmd = priv;
	int i;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "cmd");
	for (i = 0; i < cmd->numargs; i++) {
		spdk_json_write_string(w, cmd->args[i]);
	}
	spdk_json_write_array_end(w);

	spdk_json_write_named_bool(w, "capture_output", cmd->capture_output);

	spdk_json_write_object_end(w);
}

int
sto_rpc_subprocess_cmd_run(struct sto_rpc_subprocess_cmd *cmd)
{
	struct sto_client_args args = {
		.priv = cmd,
		.response_handler = sto_rpc_subprocess_resp_handler,
	};

	return sto_client_send("subprocess", cmd, sto_rpc_subprocess_info_json, &args);
}

int
sto_rpc_subprocess(const char *const argv[], int numargs,
		   sto_rpc_subprocess_done_t done, void *priv)
{
	struct sto_rpc_subprocess_cmd *cmd;
	int rc = 0;

	cmd = sto_rpc_subprocess_cmd_alloc(argv, numargs, false);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to create subprocess\n");
		return -ENOMEM;
	}

	sto_rpc_subprocess_cmd_init_cb(cmd, done, priv);

	rc = sto_rpc_subprocess_cmd_run(cmd);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run subprocess, rc=%d\n", rc);
		goto free_cmd;
	}

	return 0;

free_cmd:
	sto_rpc_subprocess_cmd_free(cmd);

	return rc;
}
