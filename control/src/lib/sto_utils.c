#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_utils.h"
#include "sto_err.h"

struct sto_json_ctx *
sto_json_ctx_alloc(void)
{
	return rte_zmalloc(NULL, sizeof(struct sto_json_ctx), 0);
}

int
sto_json_ctx_write_cb(void *cb_ctx, const void *data, size_t size)
{
	struct sto_json_ctx *ctx = cb_ctx;
	void *end;
	ssize_t rc;

	ctx->json_size = size;

	ctx->json = calloc(1, ctx->json_size);
	if (spdk_unlikely(!ctx->json)) {
		SPDK_ERRLOG("Failed to alloc json: size=%zu\n", ctx->json_size);
		return -ENOMEM;
	}

	memcpy(ctx->json, data, ctx->json_size);

	rc = spdk_json_parse(ctx->json, ctx->json_size, NULL, 0, &end, 0);
	if (spdk_unlikely(rc < 0)) {
		SPDK_NOTICELOG("Parsing JSON failed (%zd)\n", rc);
		goto free_json;
	}

	ctx->values_cnt = rc;

	ctx->values = calloc(ctx->values_cnt, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!ctx->values)) {
		SPDK_ERRLOG("Failed to alloc json values: cnt=%zu\n",
			    ctx->values_cnt);
		rc = -ENOMEM;
		goto free_json;
	}

	rc = spdk_json_parse(ctx->json, ctx->json_size, (struct spdk_json_val *) ctx->values,
			     ctx->values_cnt, &end, 0);
	if (rc != ctx->values_cnt) {
		SPDK_ERRLOG("Parsing JSON failed (%zd)\n", rc);
		goto free_values;
	}

	return 0;

free_values:
	free((struct spdk_json_val *) ctx->values);

free_json:
	free(ctx->json);

	return rc;
}

void
sto_json_ctx_free(struct sto_json_ctx *ctx)
{
	free((struct spdk_json_val *) ctx->values);
	free(ctx->json);
	rte_free(ctx);
}

int
sto_json_decode_object_name(const struct spdk_json_val *values, char **value)
{
	const struct spdk_json_val *name_json;

	if (!values || values->type != SPDK_JSON_VAL_OBJECT_BEGIN || !values->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", values);
		return -EINVAL;
	}

	name_json = &values[1];
	if (spdk_json_decode_string(name_json, value)) {
		SPDK_ERRLOG("Failed to decode JSON object name\n");
		return -EDOM;
	}

	return 0;
}
int
sto_json_decode_object_str(const struct spdk_json_val *values,
			   const char *name, char **value)
{
	const struct spdk_json_val *name_json, *value_json;

	if (!values || values->type != SPDK_JSON_VAL_OBJECT_BEGIN || !values->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", values);
		return -EINVAL;
	}

	name_json = &values[1];
	if (!spdk_json_strequal(name_json, name)) {
		SPDK_ERRLOG("JSON object name doesn't correspond to %s\n", name);
		return -ENOENT;
	}

	value_json = &values[2];

	if (spdk_json_decode_string(value_json, value)) {
		SPDK_ERRLOG("Failed to decode string from JSON object %s\n", name);
		return -EDOM;
	}

	return 0;
}

static int
sto_json_decode_value_len(const struct spdk_json_val *values)
{
	const struct spdk_json_val *value_json;

	if (!values || values->type != SPDK_JSON_VAL_OBJECT_BEGIN || !values->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", values);
		return -EINVAL;
	}

	value_json = &values[2];

	return 1 + spdk_json_val_len(value_json);
}

const struct spdk_json_val *
sto_json_next_object(const struct spdk_json_val *values)
{
	struct spdk_json_val *obj;
	uint32_t obj_len, size;
	int val_len = 0;
	int i;

	if (!values || values->type != SPDK_JSON_VAL_OBJECT_BEGIN || !values->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", values);
		return ERR_PTR(-EINVAL);
	}

	SPDK_NOTICELOG("Start parse JSON for next object: len=%u\n", values->len);

	val_len = sto_json_decode_value_len(values);
	if (val_len < 0) {
		SPDK_ERRLOG("Failed to decode JSON value len\n");
		return ERR_PTR(val_len);
	}

	obj_len = values->len - val_len;
	if (!obj_len) {
		SPDK_NOTICELOG("Next JSON object len is equal zero: val_len=%u values_len=%u\n",
			       val_len, values->len);
		return NULL;
	}

	size = obj_len + 2;

	obj = calloc(size, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!obj)) {
		SPDK_ERRLOG("Failed to alloc next JSON object: size=%u\n", size);
		return ERR_PTR(-ENOMEM);
	}

	obj->type = SPDK_JSON_VAL_OBJECT_BEGIN;
	obj->len = obj_len;

	for (i = 1; i <= obj_len + 1; i++) {
		obj[i].start = values[i + val_len].start;
		obj[i].len = values[i + val_len].len;
		obj[i].type = values[i + val_len].type;
	}

	return obj;
}

const struct spdk_json_val *
sto_json_copy_object(const struct spdk_json_val *values)
{
	struct spdk_json_val *obj;
	uint32_t size;
	int i;

	if (!values || values->type != SPDK_JSON_VAL_OBJECT_BEGIN || !values->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", values);
		return ERR_PTR(-EINVAL);
	}

	size = values->len + 1;

	obj = calloc(values->len, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!obj)) {
		SPDK_ERRLOG("Failed to alloc next JSON object: size=%u\n", size);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i <= values->len + 1; i++) {
		obj[i].start = values[i].start;
		obj[i].len = values[i].len;
		obj[i].type = values[i].type;
	}

	return obj;
}

bool
sto_find_match_str(const char *key, const char *strings[])
{
	const char **str;

	if (!strings) {
		return false;
	}

	for (str = strings; *str; str++) {
		if (!strcmp(key, *str)) {
			return true;
		}
	}

	return false;
}
