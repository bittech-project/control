#ifndef _STO_LIB_H_
#define _STO_LIB_H_

#include <spdk/queue.h>
#include <spdk/json.h>
#include <spdk/util.h>
#include <spdk/string.h>

struct spdk_json_write_ctx;
struct sto_json_iter;

struct sto_err_context {
	int rc;
	const char *errno_msg;
};

void sto_err(struct sto_err_context *err, int rc);

void sto_status_ok(struct spdk_json_write_ctx *w);
void sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err);

int sto_decode_strtoint32(const struct spdk_json_val *val, void *out);
int sto_decode_strtouint32(const struct spdk_json_val *val, void *out);
int sto_decode_strtobool(const struct spdk_json_val *val, void *out);

enum sto_ops_param_type {
	STO_OPS_PARAM_TYPE_STR,
	STO_OPS_PARAM_TYPE_INT32,
	STO_OPS_PARAM_TYPE_UINT32,
	STO_OPS_PARAM_TYPE_BOOL,
	STO_OPS_PARAM_TYPE_CNT,
};

const char *sto_ops_param_type_name(enum sto_ops_param_type type);

struct sto_json_param_raw {
	const char *name;
	enum sto_ops_param_type type;
	union {
		char *str;
		int32_t i32;
		uint32_t u32;
	} val;

	LIST_ENTRY(sto_json_param_raw) list;
};

struct sto_json_head_raw {
	const char *component_name;
	const char *object_name;
	const char *op_name;

	LIST_HEAD(, sto_json_param_raw) params;
};

#define STO_JSON_PARAM_RAW_STR(_name, _str)		\
	{						\
		.name = _name,				\
		.type = STO_OPS_PARAM_TYPE_STR,		\
		.val.str = _str,			\
	}

#define STO_JSON_PARAM_RAW_INT32(_name, _i32)		\
	{						\
		.name = _name,				\
		.type = STO_OPS_PARAM_TYPE_INT32,	\
		.val.i32 = _i32,			\
	}

#define STO_JSON_PARAM_RAW_UINT32(_name, _u32)		\
	{						\
		.name = _name,				\
		.type = STO_OPS_PARAM_TYPE_UINT32,	\
		.val.u32 = _u32,			\
	}

#define STO_JSON_PARAM_RAW_BOOL(_name, _bool)		\
	{						\
		.name = _name,				\
		.type = STO_OPS_PARAM_TYPE_BOOL,	\
		.val.i32 = _bool,			\
	}

#define STO_JSON_PARAM_RAW_TERMINATOR()			\
	{						\
		.type = STO_OPS_PARAM_TYPE_CNT,		\
	}

struct sto_json_head_raw *sto_json_head_raw(const char *component_name,
					    const char *object_name, const char *op_name);
struct sto_json_head_raw *sto_json_module_head_raw(const char *object_name, const char *op_name);
struct sto_json_head_raw *sto_json_subsystem_head_raw(const char *object_name, const char *op_name);

void sto_json_head_raw_add(struct sto_json_head_raw *head,
			   struct sto_json_param_raw params[], int params_num);
#define STO_JSON_HEAD_RAW_ADD(HEAD, PARAMS) \
	sto_json_head_raw_add((HEAD), (PARAMS), SPDK_COUNTOF(PARAMS))

#define STO_JSON_HEAD_RAW_ADD_SINGLE(HEAD, PARAM) \
	sto_json_head_raw_add((HEAD), &(struct sto_json_param_raw) PARAM, 1)

int sto_json_head_raw_dump(const struct sto_json_head_raw *head, struct spdk_json_write_ctx *w);

struct sto_ops_param_dsc {
	const char *name;
	const char *description;
	enum sto_ops_param_type type;
	bool optional;

	size_t offset;

	spdk_json_decode_fn decode;
	void (*deinit)(void *p);
};

#define STO_OPS_PARAM(MEMBER, STRUCT, DESCRIPTION, OPTIONAL, TYPE, DECODE_FUNC, DEINIT_FUNC)	\
	{											\
		.name = # MEMBER,								\
		.description = (DESCRIPTION),							\
		.type = (TYPE),									\
		.optional = (OPTIONAL),								\
		.offset = offsetof(STRUCT, MEMBER),						\
		.decode = (DECODE_FUNC),							\
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
		      STO_OPS_PARAM_TYPE_INT32, sto_decode_strtoint32, NULL)

#define STO_OPS_PARAM_INT32(MEMBER, STRUCT, DESCRIPTION)		\
	__STO_OPS_PARAM_INT32(MEMBER, STRUCT, DESCRIPTION, false)
#define STO_OPS_PARAM_INT32_OPTIONAL(MEMBER, STRUCT, DESCRIPTION)	\
	__STO_OPS_PARAM_INT32(MEMBER, STRUCT, DESCRIPTION, true)

#define __STO_OPS_PARAM_UINT32(MEMBER, STRUCT, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, STRUCT, DESCRIPTION, OPTIONAL,		\
		      STO_OPS_PARAM_TYPE_UINT32, sto_decode_strtouint32, NULL)

#define STO_OPS_PARAM_UINT32(MEMBER, STRUCT, DESCRIPTION)		\
	__STO_OPS_PARAM_UINT32(MEMBER, STRUCT, DESCRIPTION, false)
#define STO_OPS_PARAM_UINT32_OPTIONAL(MEMBER, STRUCT, DESCRIPTION)	\
	__STO_OPS_PARAM_UINT32(MEMBER, STRUCT, DESCRIPTION, true)

#define __STO_OPS_PARAM_BOOL(MEMBER, STRUCT, DESCRIPTION, OPTIONAL)	\
	STO_OPS_PARAM(MEMBER, STRUCT, DESCRIPTION, OPTIONAL,		\
		      STO_OPS_PARAM_TYPE_BOOL, sto_decode_strtobool, NULL)

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

void *sto_ops_params_decode(const struct sto_ops_params_properties *properties,
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
