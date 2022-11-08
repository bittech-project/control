#ifndef _SCST_LIB_H
#define _SCST_LIB_H

#include <spdk/util.h>

#include "scst.h"
#include "sto_tree.h"

struct scst_tg_list_req {
	struct sto_req req;

	char *dirpath;

	struct sto_tree_info info;
};

static inline struct scst_tg_list_req *
scst_tg_list_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct scst_tg_list_req, req);
}

#endif /* _SCST_LIB_H */
