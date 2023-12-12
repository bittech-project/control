#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/jsonrpc.h>

#include "sto_rpc.h"
#include "sto_srv_fs.h"
#include "sto_srv_aio.h"
#include "sto_srv_readdir.h"
#include "sto_srv_subprocess.h"

struct spdk_jsonrpc_request;

static void
sto_srv_writefile_rpc_done(void *priv, int rc)
{
	struct spdk_jsonrpc_request *request = priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", rc);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_srv_writefile_rpc(struct spdk_jsonrpc_request *request,
		      const struct spdk_json_val *params)
{
	struct sto_srv_writefile_args args = {
		.cb_arg = request,
		.cb_fn = sto_srv_writefile_rpc_done,
	};
	int rc;

	rc = sto_srv_writefile(params, &args);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto out;
	}

out:
	return;
}
STO_RPC_REGISTER("writefile", sto_srv_writefile_rpc)

static void
sto_srv_readfile_rpc_done(void *priv, char *buf, int rc)
{
	struct spdk_jsonrpc_request *request = priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", rc);
	spdk_json_write_named_string(w, "buf", buf);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_srv_readfile_rpc(struct spdk_jsonrpc_request *request,
		     const struct spdk_json_val *params)
{
	struct sto_srv_readfile_args args = {
		.cb_arg = request,
		.cb_fn = sto_srv_readfile_rpc_done,
	};
	int rc;

	rc = sto_srv_readfile(params, &args);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto out;
	}

out:
	return;
}
STO_RPC_REGISTER("readfile", sto_srv_readfile_rpc)

static void
sto_srv_readlink_rpc_done(void *priv, char *buf, int rc)
{
	struct spdk_jsonrpc_request *request = priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", rc);
	spdk_json_write_named_string(w, "buf", buf);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_srv_readlink_rpc(struct spdk_jsonrpc_request *request,
		     const struct spdk_json_val *params)
{
	struct sto_srv_readlink_args args = {
		.cb_arg = request,
		.cb_fn = sto_srv_readlink_rpc_done,
	};
	int rc;

	rc = sto_srv_readlink(params, &args);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto out;
	}

out:
	return;
}
STO_RPC_REGISTER("readlink", sto_srv_readlink_rpc)

static void
sto_srv_readdir_rpc_done(void *priv, struct sto_srv_dirents *dirents, int rc)
{
	struct spdk_jsonrpc_request *request = priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", rc);
	sto_srv_dirents_info_json(dirents, w);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_srv_readdir_rpc(struct spdk_jsonrpc_request *request,
		    const struct spdk_json_val *params)
{
	struct sto_srv_readdir_args args = {
		.cb_arg = request,
		.cb_fn = sto_srv_readdir_rpc_done,
	};
	int rc;

	rc = sto_srv_readdir(params, &args);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto out;
	}

out:
	return;
}
STO_RPC_REGISTER("readdir", sto_srv_readdir_rpc)

static void
sto_srv_subprocess_rpc_done(void *priv, char *output, int rc)
{
	struct spdk_jsonrpc_request *request = priv;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", rc);

	if (output) {
		spdk_json_write_named_string(w, "output", output);
	}

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_srv_subprocess_rpc(struct spdk_jsonrpc_request *request,
		       const struct spdk_json_val *params)
{
	struct sto_srv_subprocess_args args = {
		.cb_arg = request,
		.cb_fn = sto_srv_subprocess_rpc_done,
	};
	int rc;

	rc = sto_srv_subprocess(params, &args);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto out;
	}

out:
	return;
}
STO_RPC_REGISTER("subprocess", sto_srv_subprocess_rpc)
