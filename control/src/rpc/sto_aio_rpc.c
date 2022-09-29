#include <spdk/rpc.h>
#include <spdk/util.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_client.h"

struct sto_aio_read_params {
	char *filename;
	uint64_t size;
};

static const struct spdk_json_object_decoder sto_aio_read_decoders[] = {
	{"filename", offsetof(struct sto_aio_read_params, filename), spdk_json_decode_string},
	{"size", offsetof(struct sto_aio_read_params, size), spdk_json_decode_uint64},
};

struct sto_aio_write_params {
	char *filename;
	char *buf;
};

static const struct spdk_json_object_decoder sto_aio_write_decoders[] = {
	{"filename", offsetof(struct sto_aio_write_params, filename), spdk_json_decode_string},
	{"buf", offsetof(struct sto_aio_write_params, buf), spdk_json_decode_string},
};

struct sto_aio_read_req {
	struct spdk_jsonrpc_request *request;
	struct sto_aio_read_params params;
};

static void
sto_aio_read_free_req(struct sto_aio_read_req *req)
{
	free(req);
}

struct sto_aio_write_req {
	struct spdk_jsonrpc_request *request;
	struct sto_aio_write_params params;
};

static void
sto_aio_write_free_req(struct sto_aio_write_req *req)
{
	free(req);
}

struct sto_aio_read_result {
	int returncode;
	char *buf;
};

static const struct spdk_json_object_decoder sto_aio_read_result_decoders[] = {
	{"returncode", offsetof(struct sto_aio_read_result, returncode), spdk_json_decode_int32},
	{"buf", offsetof(struct sto_aio_read_result, buf), spdk_json_decode_string},
};

struct sto_aio_write_result {
	int returncode;
};

static const struct spdk_json_object_decoder sto_aio_write_result_decoders[] = {
	{"returncode", offsetof(struct sto_aio_write_result, returncode), spdk_json_decode_int32},
};

static void
sto_aio_read_response(struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
__read_resp_handler(struct sto_rpc_request *rpc_req, struct spdk_jsonrpc_client_response *resp)
{
	struct sto_aio_read_req *req = rpc_req->priv;
	struct sto_aio_read_result result;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_aio_read_result_decoders,
				    SPDK_COUNTOF(sto_aio_read_result_decoders), &result)) {
		spdk_jsonrpc_send_error_response(req->request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto out;
	}

	SPDK_NOTICELOG("GLEB: AIO: Get result from READ response: returncode[%d] buf: %s\n",
		       result.returncode, result.buf);

	sto_aio_read_response(req->request);

out:
	sto_rpc_req_free(rpc_req);

	sto_aio_read_free_req(req);
}

static void
sto_aio_read_info_json(struct sto_rpc_request *rpc_req, struct spdk_json_write_ctx *w)
{
	struct sto_aio_read_req *req = rpc_req->priv;
	struct sto_aio_read_params *params = &req->params;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filename", params->filename);
	spdk_json_write_named_uint64(w, "size", params->size);

	spdk_json_write_object_end(w);
}

static void
sto_aio_read(struct spdk_jsonrpc_request *request,
	     const struct spdk_json_val *params)
{
	struct sto_aio_read_req *req;
	struct sto_aio_read_params *aio_params;
	struct sto_rpc_request *rpc_req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	aio_params = &req->params;

	if (spdk_json_decode_object(params, sto_aio_read_decoders,
				    SPDK_COUNTOF(sto_aio_read_decoders), aio_params)) {
		SPDK_ERRLOG("spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	rpc_req = sto_rpc_req_alloc("aio_read", sto_aio_read_info_json, req);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to allocate RPC req\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Allocation failure");
		goto free_req;

	}

	sto_rpc_req_init_cb(rpc_req, __read_resp_handler);

	sto_client_send(rpc_req);

	return;

free_req:
	sto_aio_read_free_req(req);

	return;
}
SPDK_RPC_REGISTER("aio_read", sto_aio_read, SPDK_RPC_RUNTIME)

static void
sto_aio_write_response(struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_string(w, "GLEB");

	spdk_jsonrpc_end_result(request, w);
}

static void
__write_resp_handler(struct sto_rpc_request *rpc_req, struct spdk_jsonrpc_client_response *resp)
{
	struct sto_aio_write_req *req = rpc_req->priv;
	struct sto_aio_write_result result;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_aio_write_result_decoders,
				    SPDK_COUNTOF(sto_aio_write_result_decoders), &result)) {
		spdk_jsonrpc_send_error_response(req->request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto out;
	}

	SPDK_NOTICELOG("GLEB: AIO: Get result from WRITE response: returncode[%d]\n",
		       result.returncode);

	sto_aio_write_response(req->request);

out:
	sto_rpc_req_free(rpc_req);

	sto_aio_write_free_req(req);
}

static void
sto_aio_write_info_json(struct sto_rpc_request *rpc_req, struct spdk_json_write_ctx *w)
{
	struct sto_aio_write_req *req = rpc_req->priv;
	struct sto_aio_write_params *params = &req->params;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filename", params->filename);
	spdk_json_write_named_string(w, "buf", params->buf);

	spdk_json_write_object_end(w);
}

static void
sto_aio_write(struct spdk_jsonrpc_request *request,
	      const struct spdk_json_val *params)
{
	struct sto_aio_write_req *req;
	struct sto_aio_write_params *aio_params;
	struct sto_rpc_request *rpc_req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Memory allocation failure");
		return;
	}

	aio_params = &req->params;

	if (spdk_json_decode_object(params, sto_aio_write_decoders,
				    SPDK_COUNTOF(sto_aio_write_decoders), aio_params)) {
		SPDK_ERRLOG("spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto free_req;
	}

	req->request = request;

	rpc_req = sto_rpc_req_alloc("aio_write", sto_aio_write_info_json, req);
	if (spdk_unlikely(!rpc_req)) {
		SPDK_ERRLOG("Failed to allocate RPC req\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "Allocation failure");
		goto free_req;

	}

	sto_rpc_req_init_cb(rpc_req, __write_resp_handler);

	sto_client_send(rpc_req);

	return;

free_req:
	sto_aio_write_free_req(req);

	return;
}
SPDK_RPC_REGISTER("aio_write", sto_aio_write, SPDK_RPC_RUNTIME)
