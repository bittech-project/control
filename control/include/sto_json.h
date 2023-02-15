#ifndef _STO_JSON_H_
#define _STO_JSON_H_

#include <spdk/stdinc.h>
#include <spdk/json.h>

typedef int (*sto_json_ctx_dump_t)(void *priv, struct spdk_json_write_ctx *w);

struct sto_json_ctx {
	void *buf;
	size_t size;

	const struct spdk_json_val *values;
};

struct sto_json_iter {
	const struct spdk_json_val *values;
	int offset;
	int len;
};

struct sto_json_str_field {
	char *name;
	char *value;
};

int sto_json_ctx_dump(struct sto_json_ctx *ctx, bool formatted,
		      void *priv, sto_json_ctx_dump_t dump);
void sto_json_ctx_destroy(struct sto_json_ctx *ctx);

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

int sto_json_iter_decode_str_field(const struct sto_json_iter *iter,
				   struct sto_json_str_field *field);

static inline void
sto_json_str_field_destroy(struct sto_json_str_field *field)
{
	free(field->name);
	free(field->value);
}

void sto_json_print(const struct spdk_json_val *values);

bool sto_find_match_str(const char *key, const char *strings[]);

#endif /* _STO_JSON_H_ */
