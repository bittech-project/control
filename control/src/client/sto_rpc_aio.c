#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_client.h"
#include "sto_rpc_aio.h"

struct sto_rpc_writefile_info {
	int returncode;
};

static const struct spdk_json_object_decoder sto_rpc_writefile_info_decoders[] = {
	{"returncode", offsetof(struct sto_rpc_writefile_info, returncode), spdk_json_decode_int32},
};

struct sto_rpc_writefile_params {
	const char *filepath;
	char *buf;
};

struct sto_rpc_writefile_cmd {
	void *priv;
	sto_rpc_writefile_done_t done;
};

static struct sto_rpc_writefile_cmd *
sto_rpc_writefile_cmd_alloc(void)
{
	struct sto_rpc_writefile_cmd *cmd;

	cmd = rte_zmalloc(NULL, sizeof(*cmd), 0);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for STO RPC writefile cmd\n");
		return NULL;
	}

	return cmd;
}

static void
sto_rpc_writefile_cmd_init_cb(struct sto_rpc_writefile_cmd *cmd,
			      sto_rpc_writefile_done_t done, void *priv)
{
	cmd->done = done;
	cmd->priv = priv;
}

static void
sto_rpc_writefile_cmd_free(struct sto_rpc_writefile_cmd *cmd)
{
	rte_free(cmd);
}

static void
sto_rpc_writefile_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct sto_rpc_writefile_cmd *cmd = priv;
	struct sto_rpc_writefile_info info = {};

	if (spdk_unlikely(rc)) {
		goto out;
	}

	if (spdk_json_decode_object(resp->result, sto_rpc_writefile_info_decoders,
				    SPDK_COUNTOF(sto_rpc_writefile_info_decoders), &info)) {
		SPDK_ERRLOG("Failed to decode response for STO RPC writefile cmd\n");
		rc = -ENOMEM;
		goto out;
	}

	rc = info.returncode;

	SPDK_NOTICELOG("GLEB: Get result from STO RPC writefile response: returncode[%d]\n", rc);

out:
	cmd->done(cmd->priv, rc);
	sto_rpc_writefile_cmd_free(cmd);
}

static void
sto_rpc_writefile_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_writefile_params *params = priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filepath", params->filepath);
	spdk_json_write_named_string(w, "buf", params->buf);

	spdk_json_write_object_end(w);
}

static int
sto_rpc_writefile_cmd_run(struct sto_rpc_writefile_cmd *cmd, struct sto_rpc_writefile_params *params)
{
	struct sto_client_args args = {
		.priv = cmd,
		.response_handler = sto_rpc_writefile_resp_handler,
	};

	return sto_client_send("writefile", params, sto_rpc_writefile_info_json, &args);
}

int
sto_rpc_writefile(const char *filepath, char *buf, struct sto_rpc_writefile_args *args)
{
	struct sto_rpc_writefile_cmd *cmd;
	struct sto_rpc_writefile_params params = {
		.filepath = filepath,
		.buf = buf,
	};
	int rc;

	cmd = sto_rpc_writefile_cmd_alloc();
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to allocate RPC writefile cmd\n");
		return -ENOMEM;
	}

	sto_rpc_writefile_cmd_init_cb(cmd, args->done, args->priv);

	rc = sto_rpc_writefile_cmd_run(cmd, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run RPC writefile cmd, rc=%d\n", rc);
		goto free_cmd;
	}

	return 0;

free_cmd:
	sto_rpc_writefile_cmd_free(cmd);

	return rc;
}
