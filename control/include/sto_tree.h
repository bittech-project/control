#ifndef _STO_TREE_H_
#define _STO_TREE_H_

#include <spdk/queue.h>

#include "sto_rpc_readdir.h"

struct sto_inode {
	struct sto_inode *root;
	struct sto_inode *parent;
	struct sto_inode *cur_child;

	struct sto_dirent dirent;
	char *path;

	struct sto_dirents dirents;

	uint32_t level;

	TAILQ_ENTRY(sto_inode) list;
	TAILQ_HEAD(, sto_inode) childs;
};

struct sto_tree_cmd;

struct sto_tree_info {
	int returncode;

	struct sto_inode tree_root;

	struct {
		struct sto_tree_cmd *cmd;
	} inner;
};

typedef void (*tree_cmd_done_t)(void *priv);

struct sto_tree_cmd_args {
	void *priv;
	tree_cmd_done_t tree_cmd_done;

	struct sto_tree_info *info;
};

struct sto_tree_cmd {
	struct {
		const char *dirpath;
		uint32_t depth;
	};

	struct sto_tree_info *info;

	uint32_t refcnt;

	void *priv;
	tree_cmd_done_t tree_cmd_done;
};

void sto_tree_info_free(struct sto_tree_info *info);
void sto_tree_info_json(struct sto_tree_info *info, struct spdk_json_write_ctx *w);

int sto_tree(const char *dirpath, uint32_t depth, struct sto_tree_cmd_args *args);

#endif /* _STO_TREE_H_ */
