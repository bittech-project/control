#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "sto_err.h"

void
sto_err(struct sto_err_context *err, int rc)
{
	err->rc = rc;
	err->errno_msg = spdk_strerror(-rc);
}

void
sto_status_ok(struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "status", "OK");

	spdk_json_write_object_end(w);
}

void
sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "status", "FAILED");
	spdk_json_write_named_int32(w, "error", err->rc);
	spdk_json_write_named_string(w, "msg", err->errno_msg);

	spdk_json_write_object_end(w);
}

void *
sto_ops_decoder_params_parse(const struct sto_ops_decoder *decoder, const struct spdk_json_val *values)
{
	void *ops_params;
	uint32_t params_size;

	if (!values) {
		return decoder->allow_empty ? NULL : ERR_PTR(-EINVAL);
	}

	params_size = decoder->params_size;

	ops_params = rte_zmalloc(NULL, params_size, 0);
	if (spdk_unlikely(!ops_params)) {
		SPDK_ERRLOG("Failed to alloc ops decoder params\n");
		return ERR_PTR(-ENOMEM);
	}

	if (spdk_json_decode_object(values, decoder->decoders, decoder->num_decoders, ops_params)) {
		SPDK_ERRLOG("Failed to decode ops_params\n");
		sto_ops_decoder_params_free(decoder, ops_params);
		return ERR_PTR(-EINVAL);
	}

	return ops_params;
}

void
sto_ops_decoder_params_free(const struct sto_ops_decoder *decoder, void *ops_params)
{
	if (!ops_params) {
		return;
	}

	if (decoder->params_deinit) {
		decoder->params_deinit(ops_params);
	}

	rte_free(ops_params);
}
