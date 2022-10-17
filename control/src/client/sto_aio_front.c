#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_client.h"
#include "sto_aio_front.h"

struct sto_aio *
sto_aio_alloc(const char *filename, void *buf, size_t size, int dir)
{
	struct sto_aio *aio;

	aio = rte_zmalloc(NULL, sizeof(*aio), 0);
	if (spdk_unlikely(!aio)) {
		SPDK_ERRLOG("Cann't allocate memory for STO aio\n");
		return NULL;
	}

	aio->filename = strdup(filename);
	if (spdk_unlikely(!aio->filename)) {
		SPDK_ERRLOG("Cann't allocate memory for filename: %s\n", filename);
		goto free_aio;
	}

	aio->buf = buf;
	aio->size = size;
	aio->dir = dir;

	return aio;

free_aio:
	rte_free(aio);

	return NULL;
}

void
sto_aio_init_cb(struct sto_aio *aio, aio_end_io_t aio_end_io, void *priv)
{
	aio->aio_end_io = aio_end_io;
	aio->priv = priv;
}

void
sto_aio_free(struct sto_aio *aio)
{
	free((char *) aio->filename);
	rte_free(aio);
}

struct sto_aio_read_result {
	int returncode;
	char *buf;
};

static void
sto_aio_read_result_free(struct sto_aio_read_result *result)
{
	free(result->buf);
}

static const struct spdk_json_object_decoder sto_aio_read_result_decoders[] = {
	{"returncode", offsetof(struct sto_aio_read_result, returncode), spdk_json_decode_int32},
	{"buf", offsetof(struct sto_aio_read_result, buf), spdk_json_decode_string},
};

static void
sto_aio_read_resp_handler(struct sto_rpc_request *rpc_req,
			  struct spdk_jsonrpc_client_response *resp)
{
	struct sto_aio *aio = rpc_req->priv;
	struct sto_aio_read_result result;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_aio_read_result_decoders,
				    SPDK_COUNTOF(sto_aio_read_result_decoders), &result)) {
		SPDK_ERRLOG("Failed to decode response for subprocess\n");
		goto out;
	}

	aio->returncode = result.returncode;

	memcpy(aio->buf, result.buf, strlen(result.buf));

	SPDK_NOTICELOG("GLEB: Get result from AIO READ response: returncode[%d] buf: %s\n",
		       aio->returncode, aio->buf);

	sto_aio_read_result_free(&result);

out:
	sto_rpc_req_free(rpc_req);

	aio->aio_end_io(aio);
}

static void
sto_aio_read_info_json(struct sto_rpc_request *rpc_req, struct spdk_json_write_ctx *w)
{
	struct sto_aio *aio = rpc_req->priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filename", aio->filename);
	spdk_json_write_named_uint64(w, "size", aio->size);

	spdk_json_write_object_end(w);
}

struct sto_aio_write_result {
	int returncode;
};

static const struct spdk_json_object_decoder sto_aio_write_result_decoders[] = {
	{"returncode", offsetof(struct sto_aio_write_result, returncode), spdk_json_decode_int32},
};

static void
sto_aio_write_resp_handler(struct sto_rpc_request *rpc_req,
			   struct spdk_jsonrpc_client_response *resp)
{
	struct sto_aio *aio = rpc_req->priv;
	struct sto_aio_write_result result;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_aio_write_result_decoders,
				    SPDK_COUNTOF(sto_aio_write_result_decoders), &result)) {
		SPDK_ERRLOG("Failed to decode response for subprocess\n");
		goto out;
	}

	aio->returncode = result.returncode;

	SPDK_NOTICELOG("GLEB: Get result from AIO WRITE response: returncode[%d]\n",
		       aio->returncode);

out:
	sto_rpc_req_free(rpc_req);

	aio->aio_end_io(aio);
}

static void
sto_aio_write_info_json(struct sto_rpc_request *rpc_req, struct spdk_json_write_ctx *w)
{
	struct sto_aio *aio = rpc_req->priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "filename", aio->filename);
	spdk_json_write_named_string(w, "buf", aio->buf);

	spdk_json_write_object_end(w);
}

static struct sto_rpc_request *
__alloc_rpc_read_req(struct sto_aio *aio)
{
	struct sto_rpc_request *rpc_req;

	rpc_req = sto_rpc_req_alloc("aio_read", sto_aio_read_info_json, aio);
	if (spdk_unlikely(!rpc_req)) {
		SPDK_ERRLOG("Failed to alloc AIO RPC READ req\n");
		return NULL;
	}

	sto_rpc_req_init_cb(rpc_req, sto_aio_read_resp_handler);

	return rpc_req;
}

static struct sto_rpc_request *
__alloc_rpc_write_req(struct sto_aio *aio)
{
	struct sto_rpc_request *rpc_req;

	rpc_req = sto_rpc_req_alloc("aio_write", sto_aio_write_info_json, aio);
	if (spdk_unlikely(!rpc_req)) {
		SPDK_ERRLOG("Failed to alloc AIO RPC WRITE req\n");
		return NULL;
	}

	sto_rpc_req_init_cb(rpc_req, sto_aio_write_resp_handler);

	return rpc_req;
}

int
sto_aio_submit(struct sto_aio *aio)
{
	struct sto_rpc_request *rpc_req;
	int rc = 0;

	if (aio->dir == STO_WRITE) {
		rpc_req = __alloc_rpc_write_req(aio);
	} else {
		rpc_req = __alloc_rpc_read_req(aio);
	}

	if (spdk_unlikely(!rpc_req)) {
		SPDK_ERRLOG("Failed to alloc RPC req\n");
		return -ENOMEM;
	}

	rc = sto_client_send(rpc_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to send RPC req, rc=%d\n", rc);
		sto_rpc_req_free(rpc_req);
	}

	return rc;
}

int
sto_aio_write_string(const char *filename, char *str, aio_end_io_t aio_end_io, void *priv)
{
	struct sto_aio *aio;
	int rc;

	aio = sto_aio_alloc(filename, str, strlen(str), STO_WRITE);
	if (spdk_unlikely(!aio)) {
		SPDK_ERRLOG("Failed to alloc memory for AIO\n");
		return -ENOMEM;
	}

	sto_aio_init_cb(aio, aio_end_io, priv);

	rc = sto_aio_submit(aio);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to submit AIO, rc=%d\n", rc);
		goto free_aio;
	}

	return 0;

free_aio:
	sto_aio_free(aio);

	return rc;
}
