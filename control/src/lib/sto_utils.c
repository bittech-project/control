#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>

#include "sto_utils.h"
#include "sto_err.h"

int
sto_json_iter_decode_name(const struct sto_json_iter *iter, char **value)
{
	const struct spdk_json_val *values;
	const struct spdk_json_val *name_json;

	values = sto_json_iter_ptr(iter);

	if (!values) {
		SPDK_ERRLOG("Zero length JSON object iter\n");
		return -EINVAL;
	}

	name_json = &values[0];
	if (spdk_json_decode_string(name_json, value)) {
		SPDK_ERRLOG("Failed to decode JSON object name\n");
		return -EDOM;
	}

	return 0;
}

int
sto_json_iter_decode_str(const struct sto_json_iter *iter, const char *name, char **value)
{
	const struct spdk_json_val *values;
	const struct spdk_json_val *name_json, *value_json;

	if (iter->len < 2) {
		SPDK_ERRLOG("JSON iter length < 2: %d\n", iter->len);
		return -EINVAL;
	}

	values = sto_json_iter_ptr(iter);

	name_json = &values[0];
	if (!spdk_json_strequal(name_json, name)) {
		SPDK_ERRLOG("JSON object name doesn't correspond to %s\n", name);
		return -ENOENT;
	}

	value_json = &values[1];
	if (spdk_json_decode_string(value_json, value)) {
		SPDK_ERRLOG("Failed to decode string from JSON object %s\n", name);
		return -EDOM;
	}

	return 0;
}

int
sto_json_iter_next(struct sto_json_iter *iter)
{
	const struct spdk_json_val *values;
	int val_len;

	if (iter->len < 2) {
		SPDK_ERRLOG("JSON iter length < 2: %d\n", iter->len);
		return -EINVAL;
	}

	values = sto_json_iter_ptr(iter);

	val_len = 1 + spdk_json_val_len(&values[1]);

	iter->offset += val_len;
	iter->len -= val_len;

	return 0;
}

const struct spdk_json_val *
sto_json_iter_cut_tail(const struct sto_json_iter *iter)
{
	const struct spdk_json_val *values;
	struct spdk_json_val *obj;
	uint32_t size;
	int i;

	values = sto_json_iter_ptr(iter);
	if (!values) {
		SPDK_NOTICELOG("Next JSON object len is equal zero\n");
		return NULL;
	}

	SPDK_NOTICELOG("Start parse JSON for next object: len=%u\n", iter->len);

	size = iter->len + 2;

	obj = calloc(size, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!obj)) {
		SPDK_ERRLOG("Failed to alloc next JSON object: size=%u\n", size);
		return ERR_PTR(-ENOMEM);
	}

	obj->type = SPDK_JSON_VAL_OBJECT_BEGIN;
	obj->len = iter->len;

	for (i = 1; i <= iter->len + 1; i++) {
		obj[i].start = values[i - 1].start;
		obj[i].len = values[i - 1].len;
		obj[i].type = values[i - 1].type;
	}

	return obj;
}

struct json_write_buf {
	char data[1024];
	unsigned cur_off;
};

static int
__json_write_stdout(void *cb_ctx, const void *data, size_t size)
{
	struct json_write_buf *buf = cb_ctx;
	size_t rc;

	rc = snprintf(buf->data + buf->cur_off, sizeof(buf->data) - buf->cur_off,
		      "%s", (const char *)data);
	if (rc > 0) {
		buf->cur_off += rc;
	}
	return rc == size ? 0 : -1;
}

void
sto_json_print(const struct spdk_json_val *values)
{
	struct json_write_buf buf = {};
	struct spdk_json_write_ctx *w;

	w = spdk_json_write_begin(__json_write_stdout,
				  &buf, SPDK_JSON_PARSE_FLAG_DECODE_IN_PLACE);
	if (spdk_unlikely(!w)) {
		SPDK_ERRLOG("Failed to alloc SPDK JSON write context\n");
		return;
	}

	spdk_json_write_val(w, values);

	spdk_json_write_end(w);

	SPDK_ERRLOG("JSON: \n%s\n", buf.data);
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

/* split string into tokens */
int
sto_strsplit(char *string, int stringlen,
	     char **tokens, int maxtokens, char delim)
{
	int i, tok = 0;
	int tokstart = 1; /* first token is right at start of string */

	if (string == NULL || tokens == NULL) {
		goto einval_error;
	}

	for (i = 0; i < stringlen; i++) {
		if (string[i] == '\0' || tok >= maxtokens) {
			break;
		}
		if (tokstart) {
			tokstart = 0;
			tokens[tok++] = &string[i];
		}
		if (string[i] == delim) {
			string[i] = '\0';
			tokstart = 1;
		}
	}
	return tok;

einval_error:
	errno = EINVAL;
	return -1;
}
