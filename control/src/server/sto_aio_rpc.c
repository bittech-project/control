#include <spdk/util.h>
#include <spdk/likely.h>

#include "sto_server.h"
#include "sto_aio.h"

struct sto_aio_read_params {
	char *filename;
	uint64_t size;
};

struct sto_aio_write_params {
	char *filename;
	uint64_t size;
	char *buf;
};

static const struct spdk_json_object_decoder sto_aio_read_decoders[] = {
	{"filename", offsetof(struct sto_aio_read_params, filename), spdk_json_decode_string},
	{"size", offsetof(struct sto_aio_read_params, size), spdk_json_decode_uint64},
};

static const struct spdk_json_object_decoder sto_aio_write_decoders[] = {
	{"filename", offsetof(struct sto_aio_write_params, filename), spdk_json_decode_string},
	{"buf", offsetof(struct sto_aio_write_params, buf), spdk_json_decode_string},
};

struct sto_aio_read_req {
	struct spdk_jsonrpc_request *request;

	struct sto_aio_read_params params;
	char *buf;
};

static void
sto_aio_read_req_free(struct sto_aio_read_req *req)
{
	free(req);
}

struct sto_aio_write_req {
	struct spdk_jsonrpc_request *request;
	struct sto_aio_write_params params;
};

static void
sto_aio_write_req_free(struct sto_aio_write_req *req)
{
	free(req);
}

static void
sto_aio_read_response(struct sto_aio *aio, struct sto_aio_read_req *req)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(req->request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", aio->rc);
	spdk_json_write_named_string(w, "buf", req->buf);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(req->request, w);
}

static void
sto_aio_read_end_io(struct sto_aio *aio)
{
	struct sto_aio_read_req *req = aio->priv;

	sto_aio_read_response(aio, req);

	free(req->buf);

	sto_aio_free(aio);

	sto_aio_read_req_free(req);
}

static void
sto_aio_read(struct spdk_jsonrpc_request *request,
	     const struct spdk_json_val *params)
{
	struct sto_aio_read_req *req;
	struct sto_aio_read_params *aio_params;
	struct sto_aio *aio;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	aio_params = &req->params;

	if (spdk_json_decode_object(params, sto_aio_read_decoders,
				    SPDK_COUNTOF(sto_aio_read_decoders), aio_params)) {
		printf("spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	req->buf = calloc(1, aio_params->size);
	if (spdk_unlikely(!req->buf)) {
		printf("Failed to allocate buffer for AIO READ\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Allocation failure");
		goto free_req;
	}

	aio = sto_aio_alloc(aio_params->filename, req->buf, aio_params->size, STO_READ);
	if (spdk_unlikely(!aio)) {
		printf("Failed to allocate AIO request\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Allocation failure");
		goto free_req;
	}

	sto_aio_init_cb(aio, sto_aio_read_end_io, req);

	sto_aio_submit(aio);

	return;

free_req:
	sto_aio_read_req_free(req);

	return;
}
STO_RPC_REGISTER("aio_read", sto_aio_read)

static void
sto_aio_write_response(struct sto_aio *aio, struct sto_aio_write_req *req)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(req->request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", aio->rc);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(req->request, w);
}

static void
sto_aio_write_end_io(struct sto_aio *aio)
{
	struct sto_aio_write_req *req= aio->priv;

	sto_aio_write_response(aio, req);

	sto_aio_free(aio);

	sto_aio_write_req_free(req);
}

static void
sto_aio_write(struct spdk_jsonrpc_request *request,
	      const struct spdk_json_val *params)
{
	struct sto_aio_write_req *req;
	struct sto_aio_write_params *aio_params;
	struct sto_aio *aio;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	aio_params = &req->params;

	if (spdk_json_decode_object(params, sto_aio_write_decoders,
				    SPDK_COUNTOF(sto_aio_write_decoders), aio_params)) {
		printf("spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	aio = sto_aio_alloc(aio_params->filename, aio_params->buf, strlen(aio_params->buf), STO_WRITE);
	if (spdk_unlikely(!aio)) {
		printf("Failed to allocate AIO request\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Allocation failure");
		goto free_req;
	}

	sto_aio_init_cb(aio, sto_aio_write_end_io, req);

	sto_aio_submit(aio);

	return;

free_req:
	sto_aio_write_req_free(req);

	return;
}
STO_RPC_REGISTER("aio_write", sto_aio_write)
