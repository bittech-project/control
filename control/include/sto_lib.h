#ifndef _STO_LIB_H_
#define _STO_LIB_H_

#include <spdk/util.h>

#include "sto_hash.h"

struct spdk_json_write_ctx;
struct sto_json_iter;

struct sto_err_context {
	int rc;
	const char *errno_msg;
};

void sto_err(struct sto_err_context *err, int rc);

void sto_status_ok(struct spdk_json_write_ctx *w);
void sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err);

struct sto_ops_param_dsc {
	const char *name;
	size_t offset;
	spdk_json_decode_fn decode_func;
	bool optional;

	const char *description;
	void (*deinit)(void *p);
};

#define STO_OPS_PARAM(MEMBER, TYPE, DECODE_FUNC, OPTIONAL, DESCRIPTION, DEINIT_FUNC)	\
	{										\
		.name = # MEMBER,							\
		.offset = offsetof(TYPE, MEMBER), 					\
		.decode_func = (DECODE_FUNC),						\
		.optional = (OPTIONAL), 						\
		.description = (DESCRIPTION),						\
		.deinit = (DEINIT_FUNC),						\
	}

static inline void
sto_ops_param_str_deinit(void *p)
{
	char **s = p;
	free(*s);
}

#define __STO_OPS_PARAM_STR(MEMBER, TYPE, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, TYPE, spdk_json_decode_string, OPTIONAL, DESCRIPTION, sto_ops_param_str_deinit)

#define STO_OPS_PARAM_STR(MEMBER, TYPE, DESCRIPTION)	\
	__STO_OPS_PARAM_STR(MEMBER, TYPE, DESCRIPTION, false)
#define STO_OPS_PARAM_STR_OPTIONAL(MEMBER, TYPE, DESCRIPTION)	\
	__STO_OPS_PARAM_STR(MEMBER, TYPE, DESCRIPTION, true)

#define __STO_OPS_PARAM_INT32(MEMBER, TYPE, DESCRIPTION, OPTIONAL) \
	STO_OPS_PARAM(MEMBER, TYPE, spdk_json_decode_int32, OPTIONAL, DESCRIPTION, NULL)

#define STO_OPS_PARAM_INT32(MEMBER, TYPE, DESCRIPTION)	\
	__STO_OPS_PARAM_INT32(MEMBER, TYPE, DESCRIPTION, false)
#define STO_OPS_PARAM_INT32_OPTIONAL(MEMBER, TYPE, DESCRIPTION)	\
	__STO_OPS_PARAM_INT32(MEMBER, TYPE, DESCRIPTION, true)

#define __STO_OPS_PARAM_UINT32(MEMBER, TYPE, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, TYPE, spdk_json_decode_uint32, OPTIONAL, DESCRIPTION, NULL)

#define STO_OPS_PARAM_UINT32(MEMBER, TYPE, DESCRIPTION)	\
	__STO_OPS_PARAM_UINT32(MEMBER, TYPE, DESCRIPTION, false)
#define STO_OPS_PARAM_UINT32_OPTIONAL(MEMBER, TYPE, DESCRIPTION)	\
	__STO_OPS_PARAM_UINT32(MEMBER, TYPE, DESCRIPTION, true)

#define __STO_OPS_PARAM_BOOL(MEMBER, TYPE, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, TYPE, spdk_json_decode_bool, OPTIONAL, DESCRIPTION, NULL)

#define STO_OPS_PARAM_BOOL(MEMBER, TYPE, DESCRIPTION)	\
	__STO_OPS_PARAM_BOOL(MEMBER, TYPE, DESCRIPTION, false)
#define STO_OPS_PARAM_BOOL_OPTIONAL(MEMBER, TYPE, DESCRIPTION)	\
	__STO_OPS_PARAM_BOOL(MEMBER, TYPE, DESCRIPTION, true)

struct sto_ops_params_properties {
	const struct sto_ops_param_dsc *descriptors;
	size_t num_descriptors;

	size_t params_size;
	bool allow_empty;
};

#define __STO_OPS_PARAMS_INITIALIZER(DESCRIPTORS, TYPE, ALLOW_EMPTY)	\
	{								\
		.descriptors = (DESCRIPTORS),				\
		.num_descriptors = SPDK_COUNTOF(DESCRIPTORS),		\
		.params_size = sizeof(TYPE),				\
		.allow_empty = ALLOW_EMPTY,				\
	}

#define STO_OPS_PARAMS_INITIALIZER(DESCRIPTORS, TYPE)		\
	__STO_OPS_PARAMS_INITIALIZER(DESCRIPTORS, TYPE, false)

#define STO_OPS_PARAMS_INITIALIZER_EMPTY(DESCRIPTORS, TYPE)	\
	__STO_OPS_PARAMS_INITIALIZER(DESCRIPTORS, TYPE, true)

void *sto_ops_params_parse(const struct sto_ops_params_properties *properties,
			   const struct sto_json_iter *iter);
void sto_ops_params_free(const struct sto_ops_params_properties *properties, void *ops_params);

typedef int (*sto_ops_req_params_constructor_t)(void *arg1, const void *arg2);

struct sto_ops {
	const char *name;
	const struct sto_ops_params_properties *params_properties;
	const struct sto_req_properties *req_properties;
	sto_ops_req_params_constructor_t req_params_constructor;
};

struct sto_op_table {
	const struct sto_ops *ops;
	size_t size;
};

#define STO_OP_TABLE_INITIALIZER(_ops)		\
	{					\
		.ops = _ops,			\
		.size = SPDK_COUNTOF(_ops),	\
	}

const struct sto_hash *sto_ops_map_alloc(const struct sto_op_table *op_table);

static inline const struct sto_ops *
sto_ops_map_find(const struct sto_hash *op_map, const char *op_name)
{
	return sto_hash_lookup(op_map, op_name, strlen(op_name));
}

#endif /* _STO_LIB_H_ */
