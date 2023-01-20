#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_lib.h"
#include "sto_component.h"
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

static void *
ops_params_parse(const struct sto_ops_params_properties *properties,
		 const struct spdk_json_val *values)
{
	const size_t num_decoders = properties->num_descriptors;
	struct spdk_json_object_decoder decoders[num_decoders];
	void *params;
	size_t i;
	int rc = 0;

	for (i = 0; i < num_decoders; i++) {
		decoders[i].name = properties->descriptors[i].name;
		decoders[i].offset = properties->descriptors[i].offset;
		decoders[i].decode_func = properties->descriptors[i].decode_func;
		decoders[i].optional = properties->descriptors[i].optional;
	}

	params = calloc(1, properties->params_size);
	if (spdk_unlikely(!params)) {
		SPDK_ERRLOG("Failed to alloc decoder params\n");
		return ERR_PTR(-ENOMEM);
	}

	if (spdk_json_decode_object(values, decoders, num_decoders, params)) {
		SPDK_ERRLOG("Failed to decode params\n");
		rc = -EINVAL;
		goto free_params;
	}

	return params;

free_params:
	sto_ops_params_free(properties, params);

	return ERR_PTR(rc);
}

void *
sto_ops_params_parse(const struct sto_ops_params_properties *properties,
		     const struct sto_json_iter *iter)
{
	const struct spdk_json_val *values;
	void *params;

	values = sto_json_iter_cut_tail(iter);
	if (IS_ERR(values)) {
		SPDK_ERRLOG("Failed to create new JSON object from iter\n");
		return ERR_CAST(values);
	}

	if (!values) {
		return properties->allow_empty ? NULL : ERR_PTR(-EINVAL);
	}

	params = ops_params_parse(properties, values);

	free((struct spdk_json_val *) values);

	return params;
}

void
sto_ops_params_free(const struct sto_ops_params_properties *properties, void *ops_params)
{
	size_t i = 0;

	if (!ops_params) {
		return;
	}

	for (i = 0; i < properties->num_descriptors; i++) {
		const struct sto_ops_param_dsc *dsc = &properties->descriptors[i];

		if (dsc->deinit) {
			void *p = (void *)(uintptr_t) (ops_params + dsc->offset);
			dsc->deinit(p);
		}
	}

	free(ops_params);
}

int
sto_ops_map_init(const struct sto_shash *ops_map, const struct sto_op_table *op_table)
{
	size_t i;
	int rc;

	rc = sto_shash_init((struct sto_shash *) ops_map, op_table->size);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init ops map, rc=%d\n", rc);
		return rc;
	}

	for (i = 0; i < op_table->size; i++) {
		const struct sto_ops *op = &op_table->ops[i];

		rc = sto_shash_add((struct sto_shash *) ops_map, op->name, strlen(op->name), op);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to add '%s' op to ops map\n", op->name);
			goto failed;
		}
	}

	return 0;

failed:
	sto_shash_destroy((struct sto_shash *) ops_map);

	return rc;
}

static inline const struct sto_shash *
get_ops_map(const char *component_name, const char *object_name)
{
	const struct sto_core_component *component;

	component = sto_core_component_find(component_name, false);
	if (spdk_unlikely(!component)) {
		return NULL;
	}

	return component->get_ops_map(object_name);
}

const struct sto_ops *
sto_ops_map_find(const struct sto_shash *ops_map, const char *op_name)
{
	const struct sto_shash *alias_ops_map;
	const struct sto_ops *op;

	op = sto_shash_lookup(ops_map, op_name, strlen(op_name));

	while (op && op->type == STO_OPS_TYPE_ALIAS) {
		alias_ops_map = get_ops_map(op->component_name, op->object_name);
		if (spdk_unlikely(!ops_map)) {
			return NULL;
		}

		op = sto_shash_lookup(alias_ops_map, op->op_name, strlen(op->op_name));
	}

	return op;
}
