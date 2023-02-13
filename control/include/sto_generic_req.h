#ifndef _STO_GENERIC_REQ_H_
#define _STO_GENERIC_REQ_H_

#include <spdk/likely.h>

#include "sto_req.h"
#include "sto_json.h"
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

#endif /* _STO_GENERIC_REQ_H_ */
