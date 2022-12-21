#ifndef _STO_LIB_H_
#define _STO_LIB_H_

#include <spdk/util.h>

struct spdk_json_write_ctx;
struct sto_json_iter;

struct sto_err_context {
	int rc;
	const char *errno_msg;
};

void sto_err(struct sto_err_context *err, int rc);

void sto_status_ok(struct spdk_json_write_ctx *w);
void sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err);

typedef void (*sto_ops_decoder_params_deinit_t)(void *params);

struct sto_ops_decoder {
	const struct spdk_json_object_decoder *decoders;
	size_t num_decoders;

	uint32_t params_size;
	sto_ops_decoder_params_deinit_t params_deinit;

	bool allow_empty;
};
#define STO_OPS_DECODER_INITIALIZER(decoders, params_size, params_deinit)	\
	{decoders, SPDK_COUNTOF(decoders), params_size, params_deinit, false}

#define STO_OPS_DECODER_INITIALIZER_EMPTY(decoders, params_size, params_deinit)	\
	{decoders, SPDK_COUNTOF(decoders), params_size, params_deinit, true}

void *sto_ops_decoder_params_parse(const struct sto_ops_decoder *decoder,
		 		   const struct sto_json_iter *iter);
void sto_ops_decoder_params_free(const struct sto_ops_decoder *decoder,
				 void *ops_params);

typedef int (*sto_ops_req_params_constructor_t)(void *arg1, void *arg2);

struct sto_ops {
	const char *name;
	const struct sto_ops_decoder *decoder;
	const struct sto_req_properties *req_properties;
	sto_ops_req_params_constructor_t req_params_constructor;
};

struct sto_op_table {
	const struct sto_ops *ops;
	size_t size;
};
#define STO_OP_TABLE_INITIALIZER(ops) {ops, SPDK_COUNTOF(ops)}

const struct sto_ops *sto_op_table_find(const struct sto_op_table *op_table, const char *op_name);

#endif /* _STO_LIB_H_ */
