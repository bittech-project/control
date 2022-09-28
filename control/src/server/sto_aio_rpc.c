#include <spdk/util.h>
#include <spdk/likely.h>

#include "sto_server.h"
#include "sto_aio.h"

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

	char *buf;
};

static void
sto_rpc_free_aio_ctx(struct sto_rpc_aio_ctx *ctx)
{
	free(ctx);
}

static void
sto_rpc_aio_send_response(struct sto_aio *aio, struct sto_rpc_aio_ctx *ctx)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(ctx->request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", aio->rc);
	spdk_json_write_named_string(w, "buf", ctx->buf);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(ctx->request, w);
}

static void
sto_rpc_aio_end_io(struct sto_aio *aio)
{
	struct sto_rpc_aio_ctx *ctx = aio->priv;

	sto_rpc_aio_send_response(aio, ctx);

	free(ctx->buf);

	sto_aio_free(aio);

	sto_rpc_free_aio_ctx(ctx);
}

static void
sto_rpc_aio(struct spdk_jsonrpc_request *request,
	    const struct spdk_json_val *params)
{
	struct sto_rpc_aio_ctx *ctx;
	struct sto_rpc_construct_aio *req;
	struct sto_aio *aio;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	req = &ctx->req;

	if (spdk_json_decode_object(params, sto_rpc_construct_aio_decoders,
				    SPDK_COUNTOF(sto_rpc_construct_aio_decoders), req)) {
		printf("spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_ctx;
	}

	ctx->request = request;

	ctx->buf = calloc(1, req->size);

	aio = sto_aio_alloc(req->filename, ctx->buf, req->size, STO_READ);

	sto_aio_init_cb(aio, sto_rpc_aio_end_io, ctx);

	sto_aio_submit(aio);

	return;

free_ctx:
	sto_rpc_free_aio_ctx(ctx);

	return;
}
STO_RPC_REGISTER("aio", sto_rpc_aio)
