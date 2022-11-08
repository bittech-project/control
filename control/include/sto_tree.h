#ifndef _STO_TREE_H_
#define _STO_TREE_H_

#include "sto_readdir_front.h"

typedef void (*tree_done_t)(void *priv);

struct sto_inode {
	struct sto_inode *root;
	struct sto_inode *parent;

	struct sto_dirent dirent;
	char *path;

	struct sto_readdir_result info;

	uint32_t level;

	TAILQ_ENTRY(sto_inode) list;
	TAILQ_HEAD(, sto_inode) childs;
};

struct sto_tree_result {
	int returncode;

	struct sto_inode tree_root;

	struct {
		struct sto_tree_req *req;
	} inner;
};

struct sto_tree_args {
	void *priv;
	tree_done_t tree_done;

	struct sto_tree_result *result;
};

struct sto_tree_req {
	struct {
		const char *dirpath;
		uint32_t depth;
	};

	struct sto_tree_result *result;

	uint32_t refcnt;

	void *priv;
	tree_done_t tree_done;
};

int sto_tree(const char *dirpath, uint32_t depth, struct sto_tree_args *args);

void sto_tree_result_free(struct sto_tree_result *result);

#endif /* _STO_TREE_H_ */
