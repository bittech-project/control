#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "sto_rpc.h"
#include "sto_srv_readdir.h"

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
		.priv = request,
		.done = sto_srv_readdir_rpc_done,
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
