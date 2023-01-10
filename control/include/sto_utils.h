#ifndef _STO_UTILS_H_
#define _STO_UTILS_H_

#include <spdk/stdinc.h>
#include <spdk/json.h>

struct spdk_json_val;

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

int sto_json_iter_decode_name(const struct sto_json_iter *iter, char **value);
int sto_json_iter_decode_str(const struct sto_json_iter *iter, const char *name, char **value);

int sto_json_iter_next(struct sto_json_iter *iter);
const struct spdk_json_val *sto_json_iter_cut_tail(const struct sto_json_iter *iter);

void sto_json_print(const struct spdk_json_val *values);

bool sto_find_match_str(const char *key, const char *strings[]);

int sto_strsplit(char *string, int stringlen, char **tokens, int maxtokens, char delim);

#endif /* _STO_UTILS_H_ */
