#ifndef _STO_UTILS_H_
#define _STO_UTILS_H_

struct spdk_json_val;

int sto_json_decode_object_name(const struct spdk_json_val *values, char **value);
int sto_json_decode_object_str(const struct spdk_json_val *values,
			       const char *name, char **value);

const struct spdk_json_val *sto_json_decode_object(const struct spdk_json_val *values);

static inline const struct spdk_json_val *
sto_json_decode_next_object(const struct spdk_json_val *values)
{
	return sto_json_decode_object(values);
}

static inline const struct spdk_json_val *
sto_json_decode_next_object_and_free(const struct spdk_json_val *values)
{
	const struct spdk_json_val *next_obj;

	next_obj = sto_json_decode_object(values);

	free((struct spdk_json_val *) values);

	return next_obj;
}

bool sto_find_match_str(const char *key, const char *strings[]);

#endif /* _STO_UTILS_H_ */
