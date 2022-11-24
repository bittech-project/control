#ifndef _STO_UTILS_H_
#define _STO_UTILS_H_

struct spdk_json_val;

int sto_json_decode_object_str(const struct spdk_json_val *values,
			       const char *name, char **value);
const struct spdk_json_val *sto_json_decode_next_object(const struct spdk_json_val *values);

#endif /* _STO_UTILS_H_ */
