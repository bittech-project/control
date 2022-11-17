#include <spdk/json.h>
#include <spdk/util.h>
#include <spdk/likely.h>

#include "sto_rpc.h"
#include "sto_srv_aio.h"

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
		.priv = request,
		.done = sto_srv_writefile_rpc_done,
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