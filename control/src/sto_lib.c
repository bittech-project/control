#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>

#include "sto_lib.h"
#include "err.h"

void
sto_err(struct sto_err_context *err, int rc)
{
	err->rc = rc;
	err->errno_msg = spdk_strerror(-rc);
}

int
sto_decode_object_str(const struct spdk_json_val *values,
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
sto_decode_value_len(const struct spdk_json_val *values)
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
sto_decode_next_cdb(const struct spdk_json_val *params)
{
	struct spdk_json_val *cdb;
	uint32_t cdb_len, size;
	int val_len = 0;
	int i;

	if (!params || params->type != SPDK_JSON_VAL_OBJECT_BEGIN || !params->len) {
		SPDK_ERRLOG("Invalid JSON %p\n", params);
		return ERR_PTR(-EINVAL);
	}

	SPDK_NOTICELOG("Start parse JSON for CDB: params_len=%u\n", params->len);

	val_len = sto_decode_value_len(params);
	if (val_len < 0) {
		SPDK_ERRLOG("Failed to decode CDB\n");
		return ERR_PTR(val_len);
	}

	cdb_len = params->len - val_len;
	if (!cdb_len) {
		SPDK_NOTICELOG("CDB len is equal zero: val_len=%u params_len=%u\n",
			       val_len, params->len);
		return NULL;
	}

	size = cdb_len + 2;

	cdb = calloc(size, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!cdb)) {
		SPDK_ERRLOG("Failed to alloc CDB: size=%u\n", size);
		return ERR_PTR(-ENOMEM);
	}

	cdb->type = SPDK_JSON_VAL_OBJECT_BEGIN;
	cdb->len = cdb_len;

	for (i = 1; i <= cdb_len + 1; i++) {
		cdb[i].start = params[i + val_len].start;
		cdb[i].len = params[i + val_len].len;
		cdb[i].type = params[i + val_len].type;
	}

	return cdb;
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

