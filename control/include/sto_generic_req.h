#ifndef _STO_GENERIC_REQ_H_
#define _STO_GENERIC_REQ_H_

#include <spdk/likely.h>

#include "sto_req.h"
#include "sto_utils.h"
#include "sto_tree.h"
#include "sto_err.h"

static inline void
sto_dummy_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

struct sto_write_req_params {
	const char *file;
	char *data;
};

extern const struct sto_req_properties sto_write_req_properties;

struct sto_read_req_params {
	const char *file;
	uint32_t size;
};

extern const struct sto_req_properties sto_read_req_properties;

struct sto_readlink_req_params {
	const char *file;
};

extern const struct sto_req_properties sto_readlink_req_properties;

struct sto_readdir_req_params {
	const char *name;
	char *dirpath;
#define EXCLUDE_LIST_MAX 20
	const char *exclude_list[EXCLUDE_LIST_MAX];
};

extern const struct sto_req_properties sto_readdir_req_properties;

struct sto_tree_req_params {
	char *dirpath;
	uint32_t depth;
	bool only_dirs;

	sto_tree_info_json_t info_json;
};

extern const struct sto_req_properties sto_tree_req_properties;

struct sto_passthrough_req_params {
	char *component;
	char *object;
	char *op;

	struct spdk_json_val *params;
};

extern const struct sto_req_properties sto_passthrough_req_properties;

static inline int
sto_passthrough_req_params_set_common(struct sto_passthrough_req_params *req_params,
				      const char *object, const char *op_name,
				      const struct sto_json_iter *iter)
{
	req_params->object = strdup(object);
	if (spdk_unlikely(!req_params->object)) {
		return -ENOMEM;
	}

	req_params->op = strdup(op_name);
	if (spdk_unlikely(!req_params->op)) {
		return -ENOMEM;
	}

	req_params->params = (struct spdk_json_val *) sto_json_iter_cut_tail(iter);
	if (IS_ERR(req_params->params)) {
		return PTR_ERR(req_params->params);
	}

	return 0;
}

static inline int
sto_passthrough_req_params_set_subsystem(struct sto_passthrough_req_params *req_params,
					 const char *subsystem, const char *op_name,
					 const struct sto_json_iter *iter)
{
	req_params->component = strdup("subsystem");
	if (spdk_unlikely(!req_params->component)) {
		return -ENOMEM;
	}

	return sto_passthrough_req_params_set_common(req_params, subsystem, op_name, iter);
}

static inline int
sto_passthrough_req_params_set_module(struct sto_passthrough_req_params *req_params,
				      const char *module, const char *op_name,
				      const struct sto_json_iter *iter)
{
	req_params->component = strdup("module");
	if (spdk_unlikely(!req_params->component)) {
		return -ENOMEM;
	}

	return sto_passthrough_req_params_set_common(req_params, module, op_name, iter);
}

#endif /* _STO_GENERIC_REQ_H_ */
