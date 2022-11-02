#ifndef _SCST_LIB_H
#define _SCST_LIB_H

#include <spdk/util.h>

#include "scst.h"
#include "sto_readdir_front.h"

struct scst_ls_tg_req {
	const char *name;
	char *dirpath;

	struct sto_readdir_result result;

	void *priv;
};

struct scst_tg_list_req {
	struct sto_req req;

	char *dirpath;

	struct sto_readdir_result driver_info;

	int driver_cnt;
	struct scst_ls_tg_req *tg_reqs;

	int refcnt;
};

static inline struct scst_tg_list_req *
to_tg_list_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct scst_tg_list_req, req);
}

#endif /* _SCST_LIB_H */
