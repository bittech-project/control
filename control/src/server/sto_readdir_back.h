#ifndef _STO_READDIR_BACK_H_
#define _STO_READDIR_BACK_H_

#include <spdk/queue.h>

#include "sto_exec.h"

struct sto_readdir_back_ctx;
typedef void (*readdir_back_done_t)(struct sto_readdir_back_ctx *ctx);

struct sto_dirent {
	char *name;
	TAILQ_ENTRY(sto_dirent) list;
};

struct sto_readdir_back_ctx {
	struct sto_exec_ctx exec_ctx;

	bool skip_hidden;

	struct {
		const char *dirname;
	};

	struct {
		int returncode;

		TAILQ_HEAD(, sto_dirent) dirent_list;
	};

	void *priv;
	readdir_back_done_t readdir_back_done;
};

int sto_readdir_back(const char *dirname, bool skip_hidden,
		     readdir_back_done_t readdir_back_done, void *priv);
void sto_readdir_back_free(struct sto_readdir_back_ctx *ctx);

#endif /* _STO_READDIR_BACK_H_ */
