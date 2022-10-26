#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "sto_rpc.h"
#include "sto_readdir_back.h"

struct sto_readdir_params {
	char *dirname;
	bool skip_hidden;
};

static void
sto_readdir_params_free(struct sto_readdir_params *params)
{
	free(params->dirname);
}

static const struct spdk_json_object_decoder sto_readdir_decoders[] = {
	{"dirname", offsetof(struct sto_readdir_params, dirname), spdk_json_decode_string},
	{"skip_hidden", offsetof(struct sto_readdir_params, skip_hidden), spdk_json_decode_bool},
};

static void
sto_readdir_response(struct sto_readdir_back_req *req, struct spdk_jsonrpc_request *request)
{
	struct sto_dirent *d;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", req->returncode);

	spdk_json_write_named_array_begin(w, "dirents");

	TAILQ_FOREACH(d, &req->dirent_list, list) {
		spdk_json_write_string(w, d->name);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_readdir_done(struct sto_readdir_back_req *req)
{
	struct spdk_jsonrpc_request *request = req->priv;

	sto_readdir_response(req, request);

	sto_readdir_back_free(req);
}

static void
sto_rpc_readdir(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct sto_readdir_params rd_params = {};
	int rc;

	if (spdk_json_decode_object(params, sto_readdir_decoders,
				    SPDK_COUNTOF(sto_readdir_decoders), &rd_params)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		return;
	}

	rc = sto_readdir_back(rd_params.dirname, rd_params.skip_hidden,
			      sto_readdir_done, request);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto out;
	}

out:
	sto_readdir_params_free(&rd_params);

	return;
}
STO_RPC_REGISTER("readdir", sto_rpc_readdir)