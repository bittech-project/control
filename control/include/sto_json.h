#ifndef _STO_JSON_H_
#define _STO_JSON_H_

#include <spdk/stdinc.h>
#include <spdk/json.h>

#include "sto_async.h"

struct sto_json_ctx {
	void *buf;
	size_t size;

	const struct spdk_json_val *values;
};

typedef int (*sto_json_ctx_write_cb_t)(void *cb_ctx, struct spdk_json_write_ctx *w);

int sto_json_ctx_write(struct sto_json_ctx *json_ctx, bool formatted,
		       sto_json_ctx_write_cb_t write_cb, void *cb_ctx);

typedef void (*sto_json_ctx_async_write_cb_t)(void *cb_ctx, struct spdk_json_write_ctx *w,
					      sto_generic_cb cb_fn, void *cb_arg);

void sto_json_ctx_async_write(struct sto_json_ctx *json_ctx, bool formatted,
			      sto_json_ctx_async_write_cb_t write_cb, void *cb_ctx,
			      sto_generic_cb cb_fn, void *cb_arg);

int sto_json_ctx_parse(struct sto_json_ctx *json_ctx);
void sto_json_ctx_destroy(struct sto_json_ctx *json_ctx);

struct sto_json_iter {
	const struct spdk_json_val *values;
	int offset;
	int len;
};

static inline void
sto_json_iter_init(struct sto_json_iter *iter, const struct spdk_json_val *values)
{
	iter->values = values;
	iter->offset = values->type == SPDK_JSON_VAL_OBJECT_BEGIN ? 1 : 0;
	iter->len = values->len;
}

static inline const struct spdk_json_val *
sto_json_iter_ptr(const struct sto_json_iter *iter)
{
	return iter->len ? iter->values + iter->offset : NULL;
}

bool sto_json_iter_next(struct sto_json_iter *iter);

#define STO_JSON_FOREACH(val, values, iter)						\
	for (sto_json_iter_init((iter), (values)),					\
	     (val) = sto_json_iter_ptr((iter));						\
	     (val) != NULL;								\
	     (val) = sto_json_iter_next((iter)) ? sto_json_iter_ptr((iter)) : NULL)

const struct spdk_json_val *sto_json_iter_cut_tail(const struct sto_json_iter *iter);

int sto_json_iter_decode_name(const struct sto_json_iter *iter, char **value);
int sto_json_iter_decode_str(const struct sto_json_iter *iter, const char *name, char **value);

struct sto_json_str_field {
	char *name;
	char *value;
};

int sto_json_iter_decode_str_field(const struct sto_json_iter *iter,
				   struct sto_json_str_field *field);

static inline void
sto_json_str_field_destroy(struct sto_json_str_field *field)
{
	free(field->name);
	free(field->value);
}

struct sto_json_async_iter;

typedef void (*sto_json_async_iterate_t)(struct sto_json_async_iter *iter);
typedef struct spdk_json_val *(*sto_json_async_iter_next_t)(struct spdk_json_val *json,
							    struct spdk_json_val *object);

struct sto_json_async_iter_opts {
	struct spdk_json_val *json;

	sto_json_async_iterate_t iterate_fn;
	sto_json_async_iter_next_t next_fn;

	void *priv;
};

struct spdk_json_val *sto_json_async_iter_get_json(struct sto_json_async_iter *iter);
void *sto_json_async_iter_get_priv(struct sto_json_async_iter *iter);
struct spdk_json_val *sto_json_async_iter_get_object(struct sto_json_async_iter *iter);

void sto_json_async_iter_start(struct sto_json_async_iter_opts *opts,
			       sto_generic_cb cb_fn, void *cb_arg);

void sto_json_async_iter_next(struct sto_json_async_iter *iter, int rc);
void sto_json_async_iter_finish(struct sto_json_async_iter *iter, int rc);

struct spdk_json_val *sto_json_array_next(struct spdk_json_val *json, struct spdk_json_val *object,
					  const char *array_name);

static inline void
sto_json_async_iterate_done(void *cb_arg, int rc)
{
	sto_json_async_iter_next(cb_arg, rc);
}

struct spdk_json_val *sto_json_parse(void *buf, size_t size);
void sto_json_print(const char *fmt, const struct spdk_json_val *values, ...);

static inline struct spdk_json_val *
sto_json_value(const struct spdk_json_val *key)
{
	return key->type == SPDK_JSON_VAL_NAME ? (struct spdk_json_val *) key + 1 : NULL;
}

bool sto_find_match_str(const char *key, const char *strings[]);

#endif /* _STO_JSON_H_ */
