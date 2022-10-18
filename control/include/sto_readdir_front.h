#ifndef _STO_READDIR_FRONT_H_
#define _STO_READDIR_FRONT_H_

#include <spdk/queue.h>

struct sto_readdir_ctx;
typedef void (*readdir_done_t)(struct sto_readdir_ctx *ctx);

struct sto_dirent {
	char *name;
	TAILQ_ENTRY(sto_dirent) list;
};

struct sto_readdir_ctx {
	struct {
		const char *dirname;
	};

	struct {
		int returncode;

		char **dirents;
		int dirent_cnt;
	};

	void *priv;
	readdir_done_t readdir_done;
};

int sto_readdir(const char *dirname, readdir_done_t readdir_done, void *priv);
void sto_readdir_free(struct sto_readdir_ctx *ctx);

#endif /* _STO_READDIR_FRONT_H_ */
