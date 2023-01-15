#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_lib.h"
#include "sto_hash.h"
#include "sto_utils.h"
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
sto_ops_decoder_params_parse(const struct sto_ops_decoder *decoder,
			     const struct sto_json_iter *iter)
{
	const struct spdk_json_val *values;
	void *params = NULL;
	int rc = 0;

	values = sto_json_iter_cut_tail(iter);
	if (IS_ERR(values)) {
		SPDK_ERRLOG("Failed to create new JSON object from iter\n");
		return ERR_CAST(values);
	}

	if (!values) {
		return decoder->allow_empty ? NULL : ERR_PTR(-EINVAL);
	}

	params = calloc(1, decoder->params_size);
	if (spdk_unlikely(!params)) {
		SPDK_ERRLOG("Failed to alloc decoder params\n");
		rc = -ENOMEM;
		goto out;
	}

	if (spdk_json_decode_object(values, decoder->decoders, decoder->num_decoders, params)) {
		SPDK_ERRLOG("Failed to decode params\n");
		rc = -EINVAL;
		goto free_params;
	}

out:
	free((struct spdk_json_val *) values);

	return rc ? ERR_PTR(rc) : params;

free_params:
	sto_ops_decoder_params_free(decoder, params);

	goto out;
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

	free(ops_params);
}

const struct sto_hash *
sto_ops_map_alloc(const struct sto_op_table *op_table)
{
	struct sto_hash *ht;
	size_t i;
	int rc;

	ht = sto_hash_alloc(op_table->size);
	if (spdk_unlikely(!ht)) {
		SPDK_ERRLOG("Failed to alloc ops map\n");
		return NULL;
	}

	for (i = 0; i < op_table->size; i++) {
		const struct sto_ops *op = &op_table->ops[i];

		rc = sto_hash_add(ht, op->name, strlen(op->name), op);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to add '%s' op to ops map\n", op->name);
			goto failed;
		}
	}

	return ht;

failed:
	sto_hash_free(ht);

	return NULL;
}
