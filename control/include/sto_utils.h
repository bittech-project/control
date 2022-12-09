#ifndef _STO_UTILS_H_
#define _STO_UTILS_H_

struct spdk_json_val;

struct sto_json_ctx {
	void *json;
	size_t json_size;
	const struct spdk_json_val *values;
	size_t values_cnt;
};

struct sto_json_ctx *sto_json_ctx_alloc(void);
int sto_json_ctx_write_cb(void *cb_ctx, const void *data, size_t size);
void sto_json_ctx_free(struct sto_json_ctx *ctx);

int sto_json_decode_object_name(const struct spdk_json_val *values, char **value);
int sto_json_decode_object_str(const struct spdk_json_val *values,
			       const char *name, char **value);

const struct spdk_json_val *sto_json_next_object(const struct spdk_json_val *values);
const struct spdk_json_val *sto_json_copy_object(const struct spdk_json_val *values);

static inline const struct spdk_json_val *
sto_json_next_object_and_free(const struct spdk_json_val *values)
{
	const struct spdk_json_val *next_obj;

	next_obj = sto_json_next_object(values);

	free((struct spdk_json_val *) values);

	return next_obj;
}

bool sto_find_match_str(const char *key, const char *strings[]);

#endif /* _STO_UTILS_H_ */
