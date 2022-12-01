#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>

#include "sto_utils.h"
#include "err.h"

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
