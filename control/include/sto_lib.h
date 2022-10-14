#ifndef _STO_LIB_H_
#define _STO_LIB_H_

#include "sto_subsystem.h"

struct sto_cdbops {
	int ops;
	const char *name;
};

struct sto_err_context {
	int rc;
	const char *errno_msg;
};

struct sto_context {
	void *priv;
	sto_subsys_response_t response;
	struct sto_err_context *err_ctx;
};

int sto_decode_object_str(const struct spdk_json_val *values,
			  const char *name, char **value);
const struct spdk_json_val *sto_decode_next_cdb(const struct spdk_json_val *params);

void sto_err(struct sto_err_context *err, int rc);

#endif /* _STO_LIB_H_ */

