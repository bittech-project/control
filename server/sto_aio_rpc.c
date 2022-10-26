#include <spdk/util.h>
#include <spdk/likely.h>

#include "sto_server.h"
#include "sto_aio_back.h"

struct sto_aio_read_params {
	char *filename;
	uint64_t size;
};

static void
sto_aio_read_params_free(struct sto_aio_read_params *params)
{
	free(params->filename);
}

static const struct spdk_json_object_decoder sto_aio_read_decoders[] = {
	{"filename", offsetof(struct sto_aio_read_params, filename), spdk_json_decode_string},
	{"size", offsetof(struct sto_aio_read_params, size), spdk_json_decode_uint64},
};

struct sto_aio_read_req {
	struct spdk_jsonrpc_request *request;
	char *str;
};

static struct sto_aio_read_req *
sto_aio_read_req_alloc(uint64_t size)
{
	struct sto_aio_read_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("Failed to alloc AIO read req\n");
		return NULL;
	}

	req->str = calloc(1, size);
	if (spdk_unlikely(!req->str)) {
		printf("Failed to alloc AIO read req str\n");
		goto free_req;
	}

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_aio_read_req_free(struct sto_aio_read_req *req)
{
	free(req->str);
	free(req);
}

static void
sto_aio_read_response(struct sto_aio_back *aio, struct sto_aio_read_req *req)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(req->request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", aio->returncode);
	spdk_json_write_named_string(w, "buf", req->str);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(req->request, w);
}

static void
sto_aio_read_end_io(struct sto_aio_back *aio)
{
	struct sto_aio_read_req *req = aio->priv;

	sto_aio_read_response(aio, req);

	sto_aio_read_req_free(req);

	sto_aio_back_free(aio);
}

static void
sto_aio_read(struct spdk_jsonrpc_request *request,
	     const struct spdk_json_val *params)
{
	struct sto_aio_read_req *req;
	struct sto_aio_read_params aio_params = {};
	struct sto_aio_back *aio;
	int rc;

	if (spdk_json_decode_object(params, sto_aio_read_decoders,
				    SPDK_COUNTOF(sto_aio_read_decoders), &aio_params)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		return;
	}

	req = sto_aio_read_req_alloc(aio_params.size);
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		goto out;
	}

	req->request = request;

	aio = sto_aio_back_alloc(aio_params.filename, req->str, aio_params.size, STO_READ);
	if (spdk_unlikely(!aio)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		goto free_req;
	}

	sto_aio_back_init_cb(aio, sto_aio_read_end_io, req);

	rc = sto_aio_back_submit(aio);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto free_aio;
	}

out:
	sto_aio_read_params_free(&aio_params);

	return;

free_aio:
	sto_aio_back_free(aio);

free_req:
	sto_aio_read_req_free(req);

	goto out;
}
STO_RPC_REGISTER("aio_read", sto_aio_read)

struct sto_aio_write_params {
	char *filename;
	char *str;
};

static void
sto_aio_write_params_free(struct sto_aio_write_params *params)
{
	free(params->filename);
	free(params->str);
}

static const struct spdk_json_object_decoder sto_aio_write_decoders[] = {
	{"filename", offsetof(struct sto_aio_write_params, filename), spdk_json_decode_string},
	{"buf", offsetof(struct sto_aio_write_params, str), spdk_json_decode_string},
};

struct sto_aio_write_req {
	struct spdk_jsonrpc_request *request;
	char *str;
};

static struct sto_aio_write_req *
sto_aio_write_req_alloc(char *str)
{
	struct sto_aio_write_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("Failed to alloc AIO write req\n");
		return NULL;
	}

	req->str = strdup(str);
	if (spdk_unlikely(!req->str)) {
		printf("Failed to copy AIO write req str\n");
		goto free_req;
	}

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_aio_write_req_free(struct sto_aio_write_req *req)
{
	free(req->str);
	free(req);
}

static void
sto_aio_write_response(struct sto_aio_back *aio, struct sto_aio_write_req *req)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(req->request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", aio->returncode);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(req->request, w);
}

static void
sto_aio_write_end_io(struct sto_aio_back *aio)
{
	struct sto_aio_write_req *req = aio->priv;

	sto_aio_write_response(aio, req);

	sto_aio_write_req_free(req);

	sto_aio_back_free(aio);
}

static void
sto_aio_write(struct spdk_jsonrpc_request *request,
	      const struct spdk_json_val *params)
{
	struct sto_aio_write_req *req;
	struct sto_aio_write_params aio_params = {};
	int rc;

	if (spdk_json_decode_object(params, sto_aio_write_decoders,
				    SPDK_COUNTOF(sto_aio_write_decoders), &aio_params)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		return;
	}

	req = sto_aio_write_req_alloc(aio_params.str);
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		goto out;
	}

	req->request = request;

	rc = sto_aio_back_write_string(aio_params.filename, req->str, sto_aio_write_end_io, req);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto free_req;
	}

out:
	sto_aio_write_params_free(&aio_params);

	return;

free_req:
	sto_aio_write_req_free(req);

	goto out;
}
STO_RPC_REGISTER("aio_write", sto_aio_write)
