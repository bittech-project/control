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

enum sto_ops_param_type {
	STO_OPS_PARAM_TYPE_STR,
	STO_OPS_PARAM_TYPE_BOOL,
	STO_OPS_PARAM_TYPE_INT32,
	STO_OPS_PARAM_TYPE_UINT32,
	STO_OPS_PARAM_TYPE_CNT,
};

const char *sto_ops_param_type_name(enum sto_ops_param_type type);

struct sto_ops_param_dsc {
	const char *name;
	size_t offset;
	spdk_json_decode_fn decode_func;
	bool optional;

	enum sto_ops_param_type type;
	const char *description;
	void (*deinit)(void *p);
};

#define STO_OPS_PARAM(MEMBER, STRUCT, DESCRIPTION, OPTIONAL, TYPE, DECODE_FUNC, DEINIT_FUNC)	\
	{											\
		.name = # MEMBER,								\
		.offset = offsetof(STRUCT, MEMBER),						\
		.decode_func = (DECODE_FUNC),							\
		.optional = (OPTIONAL),								\
		.type = (TYPE),									\
		.description = (DESCRIPTION),							\
		.deinit = (DEINIT_FUNC),							\
	}

static inline void
sto_ops_param_str_deinit(void *p)
{
	char **s = p;
	free(*s);
}

#define __STO_OPS_PARAM_STR(MEMBER, STRUCT, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, STRUCT, DESCRIPTION, OPTIONAL,		\
		      STO_OPS_PARAM_TYPE_STR, spdk_json_decode_string, sto_ops_param_str_deinit)

#define STO_OPS_PARAM_STR(MEMBER, STRUCT, DESCRIPTION)			\
	__STO_OPS_PARAM_STR(MEMBER, STRUCT, DESCRIPTION, false)
#define STO_OPS_PARAM_STR_OPTIONAL(MEMBER, STRUCT, DESCRIPTION)		\
	__STO_OPS_PARAM_STR(MEMBER, STRUCT, DESCRIPTION, true)

#define __STO_OPS_PARAM_INT32(MEMBER, STRUCT, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, STRUCT, DESCRIPTION, OPTIONAL,		\
		      STO_OPS_PARAM_TYPE_INT32, spdk_json_decode_int32, NULL)

#define STO_OPS_PARAM_INT32(MEMBER, STRUCT, DESCRIPTION)		\
	__STO_OPS_PARAM_INT32(MEMBER, STRUCT, DESCRIPTION, false)
#define STO_OPS_PARAM_INT32_OPTIONAL(MEMBER, STRUCT, DESCRIPTION)	\
	__STO_OPS_PARAM_INT32(MEMBER, STRUCT, DESCRIPTION, true)

#define __STO_OPS_PARAM_UINT32(MEMBER, STRUCT, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, STRUCT, DESCRIPTION, OPTIONAL,		\
		      STO_OPS_PARAM_TYPE_UINT32, spdk_json_decode_uint32, NULL)

#define STO_OPS_PARAM_UINT32(MEMBER, STRUCT, DESCRIPTION)		\
	__STO_OPS_PARAM_UINT32(MEMBER, STRUCT, DESCRIPTION, false)
#define STO_OPS_PARAM_UINT32_OPTIONAL(MEMBER, STRUCT, DESCRIPTION)	\
	__STO_OPS_PARAM_UINT32(MEMBER, STRUCT, DESCRIPTION, true)

#define __STO_OPS_PARAM_BOOL(MEMBER, STRUCT, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, STRUCT, DESCRIPTION, OPTIONAL,		\
		      STO_OPS_PARAM_TYPE_BOOL, spdk_json_decode_bool, NULL)

#define STO_OPS_PARAM_BOOL(MEMBER, STRUCT, DESCRIPTION)			\
	__STO_OPS_PARAM_BOOL(MEMBER, STRUCT, DESCRIPTION, false)
#define STO_OPS_PARAM_BOOL_OPTIONAL(MEMBER, STRUCT, DESCRIPTION)	\
	__STO_OPS_PARAM_BOOL(MEMBER, STRUCT, DESCRIPTION, true)

struct sto_ops_params_properties {
	const struct sto_ops_param_dsc *descriptors;
	size_t num_descriptors;

	size_t params_size;
	bool allow_empty;
};

#define __STO_OPS_PARAMS_INITIALIZER(DESCRIPTORS, STRUCT, ALLOW_EMPTY)	\
	{								\
		.descriptors = (DESCRIPTORS),				\
		.num_descriptors = SPDK_COUNTOF(DESCRIPTORS),		\
		.params_size = sizeof(STRUCT),				\
		.allow_empty = ALLOW_EMPTY,				\
	}

#define STO_OPS_PARAMS_INITIALIZER(DESCRIPTORS, STRUCT)		\
	__STO_OPS_PARAMS_INITIALIZER(DESCRIPTORS, STRUCT, false)

#define STO_OPS_PARAMS_INITIALIZER_EMPTY(DESCRIPTORS, STRUCT)	\
	__STO_OPS_PARAMS_INITIALIZER(DESCRIPTORS, STRUCT, true)

void *sto_ops_params_parse(const struct sto_ops_params_properties *properties,
			   const struct sto_json_iter *iter);
void sto_ops_params_free(const struct sto_ops_params_properties *properties, void *ops_params);

typedef int (*sto_ops_req_params_constructor_t)(void *arg1, const void *arg2);

enum sto_ops_type {
	STO_OPS_TYPE_PLAIN = 0,
	STO_OPS_TYPE_ALIAS,
	STO_OPS_TYPE_CNT,
};

struct sto_ops {
	const char *name;
	enum sto_ops_type type;

	union {
		struct {
			const char *description;
			const struct sto_ops_params_properties *params_properties;
			const struct sto_req_properties *req_properties;
			sto_ops_req_params_constructor_t req_params_constructor;
		};

		struct {
			const char *component_name;
			const char *object_name;
		};
	};
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

struct sto_shash;

int sto_ops_map_init(const struct sto_shash *ops_map, const struct sto_op_table *op_table);
const struct sto_ops *sto_ops_map_find(const struct sto_shash *ops_map, const char *op_name);

#endif /* _STO_LIB_H_ */
