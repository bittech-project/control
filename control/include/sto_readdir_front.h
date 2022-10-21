#ifndef _STO_READDIR_FRONT_H_
#define _STO_READDIR_FRONT_H_

#include <spdk/queue.h>

struct sto_readdir_req;
typedef void (*readdir_done_t)(struct sto_readdir_req *req);

struct sto_dirents {
	const char **entries;
	int cnt;
};

struct sto_readdir_req {
	struct {
		bool skip_hidden;
		const char *dirname;
	};

	struct {
		int returncode;

		struct sto_dirents dirents;
	};

	void *priv;
	readdir_done_t readdir_done;
};

int sto_dirents_init(struct sto_dirents *dirents, const char **dirent_list, int cnt);
void sto_dirents_free(struct sto_dirents *dirents);
void sto_dirents_dump_json(const char *name, const char *exclude_str,
			   struct sto_dirents *dirents, struct spdk_json_write_ctx *w);

int sto_readdir(const char *dirname, readdir_done_t readdir_done, void *priv);
void sto_readdir_free(struct sto_readdir_req *req);

#endif /* _STO_READDIR_FRONT_H_ */
