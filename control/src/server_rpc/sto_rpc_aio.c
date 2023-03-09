#include "sto_rpc_aio.h"

#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/json.h>
#include <spdk/jsonrpc.h>

#include "sto_client.h"
#include "sto_async.h"

struct spdk_json_write_ctx;

struct sto_rpc_writefile_info {
	int returncode;
};

static const struct spdk_json_object_decoder sto_rpc_writefile_info_decoders[] = {
	{"returncode", offsetof(struct sto_rpc_writefile_info, returncode), spdk_json_decode_int32},
};

struct sto_rpc_writefile_params {
	const char *filepath;
	int oflag;
	char *buf;
};

struct sto_rpc_writefile_cmd {
	void *cb_arg;
	sto_generic_cb cb_fn;
};

static struct sto_rpc_writefile_cmd *
sto_rpc_writefile_cmd_alloc(void)
{
	struct sto_rpc_writefile_cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for STO RPC writefile cmd\n");
		return NULL;
	}

	return cmd;
}

static void
sto_rpc_writefile_cmd_init_cb(struct sto_rpc_writefile_cmd *cmd,
			      sto_generic_cb cb_fn, void *cb_arg)
{
	cmd->cb_fn = cb_fn;
	cmd->cb_arg = cb_arg;
}

static void
sto_rpc_writefile_cmd_free(struct sto_rpc_writefile_cmd *cmd)
{
	free(cmd);
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

out:
	cmd->cb_fn(cmd->cb_arg, rc);
	sto_rpc_writefile_cmd_free(cmd);
}

static void
sto_rpc_writefile_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_writefile_params *params = priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filepath", params->filepath);
	spdk_json_write_named_int32(w, "oflag", params->oflag);
	spdk_json_write_named_string(w, "buf", params->buf);

	spdk_json_write_object_end(w);
}

static int
sto_rpc_writefile_cmd_run(struct sto_rpc_writefile_cmd *cmd,
			  struct sto_rpc_writefile_params *params)
{
	struct sto_client_args args = {
		.priv = cmd,
		.response_handler = sto_rpc_writefile_resp_handler,
	};

	return sto_client_send("writefile", params, sto_rpc_writefile_info_json, &args);
}

void
sto_rpc_writefile(const char *filepath, int oflag, char *buf,
		  sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_cmd *cmd;
	struct sto_rpc_writefile_params params = {
		.filepath = filepath,
		.oflag = oflag,
		.buf = buf,
	};
	int rc;

	cmd = sto_rpc_writefile_cmd_alloc();
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to allocate RPC writefile cmd\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	sto_rpc_writefile_cmd_init_cb(cmd, cb_fn, cb_arg);

	rc = sto_rpc_writefile_cmd_run(cmd, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run RPC writefile cmd, rc=%d\n", rc);
		goto free_cmd;
	}

	return;

free_cmd:
	sto_rpc_writefile_cmd_free(cmd);

	cb_fn(cb_arg, rc);
	return;
}

struct rpc_readfile_info {
	int returncode;
	char **buf;
};

static int
rpc_readfile_buf_decode(const struct spdk_json_val *val, void *out)
{
	char **buf = *(char ***) out;

	return spdk_json_decode_string(val, buf);
}

static const struct spdk_json_object_decoder rpc_readfile_info_decoders[] = {
	{"returncode", offsetof(struct rpc_readfile_info, returncode), spdk_json_decode_int32},
	{"buf", offsetof(struct rpc_readfile_info, buf), rpc_readfile_buf_decode},
};

struct rpc_readfile_params {
	const char *filepath;
	uint32_t size;
};

enum rpc_readfile_type {
	RPC_READFILE_TYPE_NONE,
	RPC_READFILE_TYPE_BASIC,
	RPC_READFILE_TYPE_WITH_BUF,
};

struct rpc_readfile_cpl {
	void *cb_arg;

	enum rpc_readfile_type type;
	union {
		struct {
			sto_rpc_readfile_complete cb_fn;
			char *buf;
		} basic;

		struct {
			sto_rpc_readfile_buf_complete cb_fn;
			char **buf;
		} with_buf;
	} u;
};

static void
rpc_readfile_call_cpl(struct rpc_readfile_cpl *cpl, int rc)
{
	switch (cpl->type) {
	case RPC_READFILE_TYPE_BASIC:
		cpl->u.basic.cb_fn(cpl->cb_arg, cpl->u.basic.buf, rc);
		break;
	case RPC_READFILE_TYPE_WITH_BUF:
		cpl->u.with_buf.cb_fn(cpl->cb_arg, rc);
		break;
	default:
		assert(0);
	};
}

struct rpc_readfile_cmd {
	struct rpc_readfile_cpl cpl;
	char **buf;
};

static int
rpc_readfile_cmd_init(struct rpc_readfile_cmd *cmd)
{
	struct rpc_readfile_cpl *cpl = &cmd->cpl;

	switch (cpl->type) {
	case RPC_READFILE_TYPE_BASIC:
		cmd->buf = &cpl->u.basic.buf;
		break;
	case RPC_READFILE_TYPE_WITH_BUF:
		cmd->buf = cpl->u.with_buf.buf;
		break;
	default:
		SPDK_ERRLOG("Got unsupported readfile type (%d)\n", cpl->type);
		return -EINVAL;
	};

	return 0;
}

static struct rpc_readfile_cmd *
rpc_readfile_cmd_alloc(struct rpc_readfile_cpl *cpl)
{
	struct rpc_readfile_cmd *cmd;
	int rc;

	cmd = calloc(1, sizeof(*cmd));
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for STO RPC readfile cmd\n");
		return NULL;
	}

	cmd->cpl = *cpl;

	rc = rpc_readfile_cmd_init(cmd);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init rpc_readfile cmd\n");
		goto free_cmd;
	}

	return cmd;

free_cmd:
	free(cmd);

	return NULL;
}

static void
rpc_readfile_cmd_free(struct rpc_readfile_cmd *cmd)
{
	free(cmd);
}

static void
rpc_readfile_cmd_complete(struct rpc_readfile_cmd *cmd, int rc)
{
	rpc_readfile_call_cpl(&cmd->cpl, rc);
	rpc_readfile_cmd_free(cmd);
}

static void
rpc_readfile_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct rpc_readfile_cmd *cmd = priv;
	struct rpc_readfile_info info = {
		.buf = cmd->buf,
	};

	if (spdk_unlikely(rc)) {
		goto out;
	}

	if (spdk_json_decode_object(resp->result, rpc_readfile_info_decoders,
				    SPDK_COUNTOF(rpc_readfile_info_decoders), &info)) {
		SPDK_ERRLOG("Failed to decode response for STO RPC readfile cmd\n");
		rc = -ENOMEM;
		goto out;
	}

	rc = info.returncode;

out:
	rpc_readfile_cmd_complete(cmd, rc);
}

static void
rpc_readfile_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct rpc_readfile_params *params = priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filepath", params->filepath);
	spdk_json_write_named_uint32(w, "size", params->size);

	spdk_json_write_object_end(w);
}

static int
rpc_readfile_cmd_run(struct rpc_readfile_cmd *cmd, struct rpc_readfile_params *params)
{
	struct sto_client_args args = {
		.priv = cmd,
		.response_handler = rpc_readfile_resp_handler,
	};

	return sto_client_send("readfile", params, rpc_readfile_info_json, &args);
}

static int
rpc_readfile(struct rpc_readfile_cpl *cpl, struct rpc_readfile_params *params)
{
	struct rpc_readfile_cmd *cmd;
	int rc;

	cmd = rpc_readfile_cmd_alloc(cpl);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to allocate RPC readfile cmd\n");
		return -ENOMEM;
	}

	rc = rpc_readfile_cmd_run(cmd, params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run RPC readfile cmd, rc=%d\n", rc);
		goto free_cmd;
	}

	return 0;

free_cmd:
	rpc_readfile_cmd_free(cmd);

	return rc;
}

void
sto_rpc_readfile(const char *filepath, uint32_t size,
		 sto_rpc_readfile_complete cb_fn, void *cb_arg)
{
	struct rpc_readfile_cpl cpl = {};
	struct rpc_readfile_params params = {
		.filepath = filepath,
		.size = size,
	};
	int rc;

	cpl.type = RPC_READFILE_TYPE_BASIC;
	cpl.u.basic.cb_fn = cb_fn;
	cpl.cb_arg = cb_arg;

	rc = rpc_readfile(&cpl, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("rpc_readfile() failed\n");
		rpc_readfile_call_cpl(&cpl, rc);
		return;
	}

	return;
}

void
sto_rpc_readfile_buf(const char *filepath, uint32_t size,
		     sto_rpc_readfile_buf_complete cb_fn, void *cb_arg,
		     char **buf)
{
	struct rpc_readfile_cpl cpl = {};
	struct rpc_readfile_params params = {
		.filepath = filepath,
		.size = size,
	};
	int rc;

	cpl.type = RPC_READFILE_TYPE_WITH_BUF;
	cpl.u.with_buf.cb_fn = cb_fn;
	cpl.cb_arg = cb_arg;
	cpl.u.with_buf.buf = buf;

	rc = rpc_readfile(&cpl, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("rpc_readfile() failed\n");
		rpc_readfile_call_cpl(&cpl, rc);
		return;
	}

	return;
}

struct sto_rpc_readlink_info {
	int returncode;
	char **buf;
};

static int
sto_rpc_readlink_buf_decode(const struct spdk_json_val *val, void *out)
{
	char **buf = *(char ***) out;

	return spdk_json_decode_string(val, buf);
}

static const struct spdk_json_object_decoder sto_rpc_readlink_info_decoders[] = {
	{"returncode", offsetof(struct sto_rpc_readlink_info, returncode), spdk_json_decode_int32},
	{"buf", offsetof(struct sto_rpc_readlink_info, buf), sto_rpc_readlink_buf_decode},
};

struct sto_rpc_readlink_params {
	const char *filepath;
};

struct sto_rpc_readlink_cmd {
	void *cb_arg;
	sto_generic_cb cb_fn;

	char **buf;
};

static struct sto_rpc_readlink_cmd *
sto_rpc_readlink_cmd_alloc(void)
{
	struct sto_rpc_readlink_cmd *cmd;

	cmd = calloc(1, sizeof(*cmd));
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for STO RPC readlink cmd\n");
		return NULL;
	}

	return cmd;
}

static void
sto_rpc_readlink_cmd_init_cb(struct sto_rpc_readlink_cmd *cmd,
			     sto_generic_cb cb_fn, void *cb_arg)
{
	cmd->cb_fn = cb_fn;
	cmd->cb_arg = cb_arg;
}

static void
sto_rpc_readlink_cmd_free(struct sto_rpc_readlink_cmd *cmd)
{
	free(cmd);
}

static void
sto_rpc_readlink_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct sto_rpc_readlink_cmd *cmd = priv;
	struct sto_rpc_readlink_info info = {
		.buf = cmd->buf,
	};

	if (spdk_unlikely(rc)) {
		goto out;
	}

	if (spdk_json_decode_object(resp->result, sto_rpc_readlink_info_decoders,
				    SPDK_COUNTOF(sto_rpc_readlink_info_decoders), &info)) {
		SPDK_ERRLOG("Failed to decode response for STO RPC readlink cmd\n");
		rc = -ENOMEM;
		goto out;
	}

	rc = info.returncode;

out:
	cmd->cb_fn(cmd->cb_arg, rc);
	sto_rpc_readlink_cmd_free(cmd);
}

static void
sto_rpc_readlink_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_readlink_params *params = priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filepath", params->filepath);

	spdk_json_write_object_end(w);
}

static int
sto_rpc_readlink_cmd_run(struct sto_rpc_readlink_cmd *cmd, struct sto_rpc_readlink_params *params)
{
	struct sto_client_args args = {
		.priv = cmd,
		.response_handler = sto_rpc_readlink_resp_handler,
	};

	return sto_client_send("readlink", params, sto_rpc_readlink_info_json, &args);
}

void
sto_rpc_readlink(const char *filepath, sto_generic_cb cb_fn, void *cb_arg, char **buf)
{
	struct sto_rpc_readlink_cmd *cmd;
	struct sto_rpc_readlink_params params = {
		.filepath = filepath,
	};
	int rc;

	cmd = sto_rpc_readlink_cmd_alloc();
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to allocate RPC readlink cmd\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	cmd->buf = buf;

	sto_rpc_readlink_cmd_init_cb(cmd, cb_fn, cb_arg);

	rc = sto_rpc_readlink_cmd_run(cmd, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run RPC readlink cmd, rc=%d\n", rc);
		goto free_cmd;
	}

	return;

free_cmd:
	sto_rpc_readlink_cmd_free(cmd);
	cb_fn(cb_arg, rc);

	return;
}
