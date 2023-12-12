#include "sto_lib.h"

#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_component.h"
#include "sto_hash.h"
#include "sto_json.h"
#include "sto_err.h"

struct spdk_json_write_ctx;

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

int
sto_decode_strtoint32(const struct spdk_json_val *val, void *out)
{
	int32_t *i = out;
	char *s = NULL;
	long long int res;
	int rc = 0;

	if (spdk_json_decode_string(val, &s)) {
		return -ENOMEM;
	}

	if (s[0] == '-') {
		res = spdk_strtoll(s + 1, 10);
		if (res < 0) {
			rc = res;
			goto out;
		}

		if ((int32_t) -res > 0) {
			rc = -ERANGE;
			goto out;
		}

		*i = (int32_t) -res;
	} else {
		res = spdk_strtoll(s, 10);
		if (res < 0) {
			rc = res;
			goto out;
		}

		if ((int32_t) res < 0) {
			rc = -ERANGE;
			goto out;
		}

		*i = (int32_t) res;
	}

out:
	free(s);

	return rc;
}

int
sto_decode_strtouint32(const struct spdk_json_val *val, void *out)
{
	uint32_t *i = out;
	char *s = NULL;
	long long int res;
	int rc = 0;

	if (spdk_json_decode_string(val, &s)) {
		return -ENOMEM;
	}

	res = spdk_strtoll(s, 10);
	if (res < 0) {
		rc = res;
		goto out;
	}

	if (res != (uint32_t) res) {
		rc = -ERANGE;
		goto out;
	}

	*i = (uint32_t) res;

out:
	free(s);

	return rc;
}

int
sto_decode_strtobool(const struct spdk_json_val *val, void *out)
{
	bool *i = out;
	char *s = NULL;
	int rc = 0;

	if (spdk_json_decode_string(val, &s)) {
		return -ENOMEM;
	}

	switch (s[0]) {
	case 'y':
	case 'Y':
	case 't':
	case 'T':
	case '1':
		*i = true;
		break;
	case 'n':
	case 'N':
	case 'f':
	case 'F':
	case '0':
		*i = false;
		break;
	case 'o':
	case 'O':
		switch (s[1]) {
		case 'n':
		case 'N':
			*i = true;
			break;
		case 'f':
		case 'F':
			*i = false;
			break;
		default:
			rc = -EINVAL;
			break;
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}

	free(s);

	return rc;
}

static const char *const ops_param_type_name[] = {
	[STO_OPS_PARAM_TYPE_STR]	= "string",
	[STO_OPS_PARAM_TYPE_INT32]	= "int32",
	[STO_OPS_PARAM_TYPE_UINT32]	= "uint32",
	[STO_OPS_PARAM_TYPE_BOOL]	= "bool",
};

const char *
sto_ops_param_type_name(enum sto_ops_param_type type)
{
	return (type < STO_OPS_PARAM_TYPE_CNT) ? ops_param_type_name[type] : "Unknown";
}

static struct sto_json_head_raw g_json_head_raw;
static struct sto_json_head_raw g_json_module_head_raw;
static struct sto_json_head_raw g_json_subsystem_head_raw;

static void
json_head_raw_init(struct sto_json_head_raw *head,
		   const char *component_name, const char *object_name, const char *op_name)
{
	memset(head, 0, sizeof(struct sto_json_head_raw));

	head->component_name = component_name;
	head->object_name = object_name;
	head->op_name = op_name;
}

struct sto_json_head_raw *
sto_json_head_raw(const char *component_name, const char *object_name, const char *op_name)
{
	json_head_raw_init(&g_json_head_raw, component_name, object_name, op_name);

	return &g_json_head_raw;
}

struct sto_json_head_raw *
sto_json_module_head_raw(const char *object_name, const char *op_name)
{
	json_head_raw_init(&g_json_module_head_raw, "module", object_name, op_name);

	return &g_json_module_head_raw;
}

struct sto_json_head_raw *
sto_json_subsystem_head_raw(const char *object_name, const char *op_name)
{
	json_head_raw_init(&g_json_subsystem_head_raw, "subsystem", object_name, op_name);

	return &g_json_subsystem_head_raw;
}

void
sto_json_head_raw_add(struct sto_json_head_raw *head,
		      struct sto_json_param_raw params[], int params_num)
{
	int i;

	for (i = 0; i < params_num; i++) {
		LIST_INSERT_HEAD(&head->params, &params[i], list);
	}
}

int
sto_json_head_raw_dump(const struct sto_json_head_raw *head,
		       struct spdk_json_write_ctx *w)
{
	const struct sto_json_param_raw *param;

	spdk_json_write_named_string(w, head->component_name, head->object_name);
	spdk_json_write_named_string(w, "op", head->op_name);

	LIST_FOREACH(param, &head->params, list) {
		spdk_json_write_name(w, param->name);

		switch (param->type) {
		case STO_OPS_PARAM_TYPE_STR:
			spdk_json_write_string_fmt(w, "%s", param->val.str);
			break;
		case STO_OPS_PARAM_TYPE_BOOL:
		case STO_OPS_PARAM_TYPE_INT32:
			spdk_json_write_string_fmt(w, "%d", param->val.i32);
			break;
		case STO_OPS_PARAM_TYPE_UINT32:
			spdk_json_write_string_fmt(w, "%u", param->val.u32);
			break;
		default:
			SPDK_ERRLOG("Unkown ops param type %d\n", param->type);
			assert(0);
			return -EINVAL;
		}
	}

	return 0;
}

static void *
ops_params_decode(const struct sto_ops_params_properties *properties,
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
		decoders[i].decode_func = properties->descriptors[i].decode;
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
sto_ops_params_decode(const struct sto_ops_params_properties *properties,
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

	params = ops_params_decode(properties, values);

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

		op = sto_shash_lookup(alias_ops_map, op->name, strlen(op->name));
	}

	return op;
}
