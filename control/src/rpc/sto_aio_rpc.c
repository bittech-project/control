#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_client.h"

struct sto_rpc_construct_aio {
	char *filename;
	uint64_t size;
	int rw;
};

static const struct spdk_json_object_decoder sto_rpc_construct_aio_decoders[] = {
	{"filename", offsetof(struct sto_rpc_construct_aio, filename), spdk_json_decode_string},
	{"size", offsetof(struct sto_rpc_construct_aio, size), spdk_json_decode_uint64},
	{"rw", offsetof(struct sto_rpc_construct_aio, rw), spdk_json_decode_int32},
};

struct sto_rpc_aio_ctx {
	struct sto_rpc_construct_aio req;

	struct spdk_jsonrpc_request *request;

	int fd;
	char *buf;
};

static void
sto_rpc_free_aio_ctx(struct sto_rpc_aio_ctx *ctx)
{
	free(ctx);
}

struct sto_rpc_aio_result {
	int returncode;
	char *buf;
};

static const struct spdk_json_object_decoder sto_rpc_aio_result_decoders[] = {
	{"returncode", offsetof(struct sto_rpc_aio_result, returncode), spdk_json_decode_int32},
	{"buf", offsetof(struct sto_rpc_aio_result, buf), spdk_json_decode_string},
};

static void
sto_rpc_aio_response(struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
__resp_handler(struct sto_rpc_request *req, struct spdk_jsonrpc_client_response *resp)
{
	struct sto_rpc_aio_ctx *ctx = req->priv;
	struct sto_rpc_aio_result result;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_rpc_aio_result_decoders,
				    SPDK_COUNTOF(sto_rpc_aio_result_decoders), &result)) {
		spdk_jsonrpc_send_error_response(ctx->request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto out;
	}

	SPDK_NOTICELOG("GLEB: AIO: Get result from response: returncode[%d] buf: %s\n",
		       result.returncode, result.buf);

	sto_rpc_aio_response(ctx->request);

out:
	sto_rpc_req_free(req);

	sto_rpc_free_aio_ctx(ctx);
}

static void
sto_aio_info_json(struct sto_rpc_request *sto_req, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_aio_ctx *ctx = sto_req->priv;
	struct sto_rpc_construct_aio *req = &ctx->req;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filename", req->filename);
	spdk_json_write_named_uint64(w, "size", req->size);
	spdk_json_write_named_int32(w, "rw", req->rw);

	spdk_json_write_object_end(w);
}

static void
sto_rpc_aio(struct spdk_jsonrpc_request *request,
	    const struct spdk_json_val *params)
{
	struct sto_rpc_aio_ctx *ctx;
	struct sto_rpc_construct_aio *req;
	struct sto_rpc_request *sto_req;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	req = &ctx->req;

	if (spdk_json_decode_object(params, sto_rpc_construct_aio_decoders,
				    SPDK_COUNTOF(sto_rpc_construct_aio_decoders), req)) {
		SPDK_ERRLOG("spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_ctx;
	}

	ctx->request = request;

	sto_req = sto_rpc_req_alloc("aio", sto_aio_info_json, ctx);
	assert(sto_req);

	sto_rpc_req_init_cb(sto_req, __resp_handler);

	sto_client_send(sto_req);

	return;

free_ctx:
	sto_rpc_free_aio_ctx(ctx);

	return;
}
SPDK_RPC_REGISTER("aio", sto_rpc_aio, SPDK_RPC_RUNTIME)
