#include "sto_json.h"

#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>

#include "sto_err.h"
#include "sto_async.h"

int
sto_json_ctx_parse(struct sto_json_ctx *json_ctx)
{
	struct spdk_json_val *values;

	values = sto_json_parse(json_ctx->buf, json_ctx->size);
	if (IS_ERR(values)) {
		SPDK_ERRLOG("Failed to parse buf for JSON context\n");
		return PTR_ERR(values);
	}

	json_ctx->values = values;

	return 0;
}

static int
json_ctx_write_cb(void *cb_ctx, const void *data, size_t size)
{
	struct sto_json_ctx *json_ctx = cb_ctx;

	json_ctx->buf = calloc(1, size);
	if (spdk_unlikely(!json_ctx->buf)) {
		SPDK_ERRLOG("Failed to alloc buf: size=%zu\n", size);
		return -ENOMEM;
	}

	json_ctx->size = size;

	memcpy(json_ctx->buf, data, size);

	return sto_json_ctx_parse(json_ctx);
}

int
sto_json_ctx_write(struct sto_json_ctx *json_ctx, bool formatted,
		   sto_json_ctx_write_cb_t write_cb, void *cb_ctx)
{
	struct spdk_json_write_ctx *w;
	uint32_t flags = formatted ? SPDK_JSON_PARSE_FLAG_DECODE_IN_PLACE : 0;
	int rc = 0;

	w = spdk_json_write_begin(json_ctx_write_cb, json_ctx, flags);
	if (spdk_unlikely(!w)) {
		SPDK_ERRLOG("Failed to alloc SPDK json write context\n");
		return -ENOMEM;
	}

	rc = write_cb(cb_ctx, w);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to dump user info to JSON context\n");
	}

	if (spdk_unlikely(spdk_json_write_end(w))) {
		SPDK_ERRLOG("Failed to write end for JSON context\n");
		rc = rc ?: -EINVAL;
	}

	if (spdk_unlikely(rc)) {
		goto out_err;
	}

	return 0;

out_err:
	sto_json_ctx_destroy(json_ctx);

	return rc;
}

struct sto_json_async_ctx {
	struct sto_json_ctx *json_ctx;
	struct spdk_json_write_ctx *w;

	sto_generic_cb cb_fn;
	void *cb_arg;
};

static inline void
sto_json_async_ctx_done(struct sto_json_async_ctx *ctx, int rc)
{
	ctx->cb_fn(ctx->cb_arg, rc);
	free(ctx);
}

static void
json_ctx_async_write_done(void *cb_arg, int rc)
{
	struct sto_json_async_ctx *ctx = cb_arg;
	struct sto_json_ctx *json_ctx = ctx->json_ctx;

	if (spdk_unlikely(spdk_json_write_end(ctx->w))) {
		SPDK_ERRLOG("Failed to write end for JSON context\n");
		rc = rc ?: -EINVAL;
	}

	if (spdk_unlikely(rc)) {
		goto out_err;
	}

out:
	sto_json_async_ctx_done(ctx, rc);

	return;

out_err:
	sto_json_ctx_destroy(json_ctx);
	goto out;
}

void
sto_json_ctx_async_write(struct sto_json_ctx *json_ctx, bool formatted,
			 sto_json_ctx_async_write_cb_t write_cb, void *cb_ctx,
			 sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_json_async_ctx *ctx;
	struct spdk_json_write_ctx *w;
	uint32_t flags = formatted ? SPDK_JSON_PARSE_FLAG_DECODE_IN_PLACE : 0;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to allod sto_json_async_ctx\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	ctx->cb_fn = cb_fn;
	ctx->cb_arg = cb_arg;

	w = spdk_json_write_begin(json_ctx_write_cb, json_ctx, flags);
	if (spdk_unlikely(!w)) {
		SPDK_ERRLOG("Failed to alloc SPDK json write context\n");
		sto_json_async_ctx_done(ctx, -ENOMEM);
		return;
	}

	ctx->json_ctx = json_ctx;
	ctx->w = w;

	write_cb(cb_ctx, w, json_ctx_async_write_done, ctx);
}

void
sto_json_ctx_destroy(struct sto_json_ctx *json_ctx)
{
	free((struct spdk_json_val *) json_ctx->values);
	json_ctx->values = NULL;

	free(json_ctx->buf);
	json_ctx->buf = NULL;
}

bool
sto_json_iter_next(struct sto_json_iter *iter)
{
	const struct spdk_json_val *values;
	int val_len;

	if (iter->len < 2) {
		return false;
	}

	values = sto_json_iter_ptr(iter);

	val_len = 1 + spdk_json_val_len(&values[1]);

	iter->offset += val_len;
	iter->len -= val_len;

	return true;
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
sto_json_iter_decode_str_field(const struct sto_json_iter *iter,
			       struct sto_json_str_field *field)
{
	char *name = NULL, *value = NULL;
	int rc;

	rc = sto_json_iter_decode_name(iter, &name);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode str field name\n");
		return rc;
	}

	rc = sto_json_iter_decode_str(iter, name, &value);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode str field %s value\n", name);
		free(name);
		return rc;
	}

	field->name = name;
	field->value = value;

	return 0;
}

struct sto_json_async_iter {
	struct sto_json_async_iter_opts opts;

	struct spdk_json_val *object;

	sto_generic_cb cb_fn;
	void *cb_arg;
};

struct spdk_json_val *
sto_json_async_iter_get_json(struct sto_json_async_iter *iter)
{
	return iter->opts.json;
}

void *
sto_json_async_iter_get_priv(struct sto_json_async_iter *iter)
{
	return iter->opts.priv;
}

struct spdk_json_val *
sto_json_async_iter_get_object(struct sto_json_async_iter *iter)
{
	return iter->object;
}

void
sto_json_async_iter_start(struct sto_json_async_iter_opts *opts,
			  sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_json_async_iter *iter;

	iter = calloc(1, sizeof(*iter));
	if (spdk_unlikely(!iter)) {
		SPDK_ERRLOG("Failed to create SCST json dev iter\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	iter->opts = *opts;

	iter->cb_fn = cb_fn;
	iter->cb_arg = cb_arg;

	sto_json_async_iter_next(iter, 0);
}

void
sto_json_async_iter_finish(struct sto_json_async_iter *iter, int rc)
{
	iter->cb_fn(iter->cb_arg, rc);
	free(iter);
}

void
sto_json_async_iter_next(struct sto_json_async_iter *iter, int rc)
{
	struct sto_json_async_iter_opts *opts = &iter->opts;

	if (rc) {
		sto_json_async_iter_finish(iter, rc);
		return;
	}

	iter->object = opts->next_fn(opts->json, iter->object);
	if (!iter->object) {
		sto_json_async_iter_finish(iter, 0);
		return;
	}

	opts->iterate_fn(iter);
}

struct spdk_json_val *
sto_json_array_next(struct spdk_json_val *json, struct spdk_json_val *object, const char *array_name)
{
	if (!object) {
		struct spdk_json_val *array = json;
		int rc = 0;

		rc = spdk_json_find_array(array, array_name, NULL, &array);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Could not find `%s` array in JSON\n", array_name);
			return NULL;
		}

		return spdk_json_array_first(array);
	}

	return spdk_json_next(object);
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
sto_json_print(const char *fmt, const struct spdk_json_val *values, ...)
{
	struct json_write_buf buf = {};
	struct spdk_json_write_ctx *w;
	va_list fmt_args;
	char *log_s;

	va_start(fmt_args, values);
	log_s = spdk_vsprintf_alloc(fmt, fmt_args);
	va_end(fmt_args);

	if (spdk_unlikely(!log_s)) {
		SPDK_ERRLOG("Failed to alloc format string for log string\n");
		return;
	}

	w = spdk_json_write_begin(__json_write_stdout,
				  &buf, SPDK_JSON_PARSE_FLAG_DECODE_IN_PLACE);
	if (spdk_unlikely(!w)) {
		SPDK_ERRLOG("Failed to alloc SPDK JSON write context\n");
		goto out;
	}

	spdk_json_write_val(w, values);

	spdk_json_write_end(w);

	SPDK_ERRLOG("%s [JSON]: \n%s\n", log_s, buf.data);

out:
	free(log_s);
}

struct spdk_json_val *
sto_json_parse(void *buf, size_t size)
{
	struct spdk_json_val *values;
	void *end;
	size_t values_cnt;
	ssize_t rc;

	rc = spdk_json_parse(buf, size, NULL, 0, &end, 0);
	if (spdk_unlikely(rc < 0)) {
		SPDK_NOTICELOG("Counting JSON failed (%zd), buf:\n\n%s\n\n",
			       rc, (char *) buf);
		return ERR_PTR(rc);
	}

	values_cnt = rc;

	values = calloc(values_cnt, sizeof(struct spdk_json_val));
	if (spdk_unlikely(!values)) {
		SPDK_ERRLOG("Failed to alloc json values: cnt=%zu\n", values_cnt);
		return ERR_PTR(-ENOMEM);
	}

	rc = spdk_json_parse(buf, size, (struct spdk_json_val *) values, values_cnt, &end, 0);
	if (rc != (ssize_t) values_cnt) {
		SPDK_ERRLOG("Parsing JSON failed (%zd)\n", rc);
		rc = -EINVAL;
		goto free_values;
	}

	return values;

free_values:
	free(values);

	return ERR_PTR(rc);
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
