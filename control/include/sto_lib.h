#ifndef _STO_LIB_H_
#define _STO_LIB_H_

#include "sto_subsystem.h"

typedef void *(*sto_params_alloc)(void);
typedef void (*sto_params_free)(void *params);

typedef int (*sto_params_parse)(void *priv, void *params);

struct sto_decoder {
	const struct spdk_json_object_decoder *decoders;
	size_t num_decoders;

	sto_params_alloc params_alloc;
	sto_params_free params_free;
};
#define STO_DECODER_INITIALIZER(decoders, params_alloc, params_free)	\
	{decoders, SPDK_COUNTOF(decoders), params_alloc, params_free}

struct sto_cdbops {
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

int sto_decoder_parse(struct sto_decoder *decoder, const struct spdk_json_val *data,
		      sto_params_parse params_parse, void *priv);

int sto_decode_object_str(const struct spdk_json_val *values,
			  const char *name, char **value);
const struct spdk_json_val *sto_decode_next_cdb(const struct spdk_json_val *params);

void sto_err(struct sto_err_context *err, int rc);

void sto_status_ok(struct spdk_json_write_ctx *w);
void sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err);

#endif /* _STO_LIB_H_ */

