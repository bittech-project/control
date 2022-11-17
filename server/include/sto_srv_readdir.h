#ifndef _STO_SRV_READDIR_H_
#define _STO_SRV_READDIR_H_

#include <spdk/queue.h>

#include "sto_exec.h"

struct spdk_json_write_ctx;
struct spdk_json_val;

struct sto_srv_dirent {
	char *name;
	uint32_t mode;

	TAILQ_ENTRY(sto_srv_dirent) list;
};

struct sto_srv_dirents {
	TAILQ_HEAD(, sto_srv_dirent) dirents;
};

typedef void (*sto_srv_readdir_done_t)(void *priv, struct sto_srv_dirents *dirents, int rc);

struct sto_srv_readdir_args {
	void *priv;
	sto_srv_readdir_done_t done;
};

int sto_srv_readdir(const struct spdk_json_val *params,
		    struct sto_srv_readdir_args *args);

void sto_srv_dirents_info_json(struct sto_srv_dirents *dirents,
			       struct spdk_json_write_ctx *w);

#endif /* _STO_SRV_READDIR_H_ */
