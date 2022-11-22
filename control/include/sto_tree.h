#ifndef _STO_TREE_H_
#define _STO_TREE_H_

#include <spdk/queue.h>

struct sto_inode;

struct sto_tree_node {
	struct sto_tree_node *root;
	struct sto_tree_node *parent;
	struct sto_tree_node *cur_child;

	struct sto_inode *inode;

	uint32_t level;

	TAILQ_ENTRY(sto_tree_node) list;
	TAILQ_HEAD(, sto_tree_node) childs;
};

struct sto_tree_info {
	int returncode;

	struct sto_tree_node tree_root;

	struct {
		void *cmd;
	} inner;
};

typedef void (*sto_tree_done_t)(void *priv);

struct sto_tree_params {
	uint32_t depth;
	bool only_dirs;
};

struct sto_tree_args {
	void *priv;
	sto_tree_done_t done;

	struct sto_tree_info *info;
};

void sto_tree_info_free(struct sto_tree_info *info);
void sto_tree_info_json(struct sto_tree_info *info, struct spdk_json_write_ctx *w);

int sto_tree(const char *dirpath, uint32_t depth, bool only_dirs, struct sto_tree_args *args);

void sto_tree_get_ref(struct sto_tree_node *node);
void sto_tree_put_ref(struct sto_tree_node *node);
void sto_tree_set_error(struct sto_tree_node *node, int rc);
bool sto_tree_check_error(struct sto_tree_node *node);
bool sto_tree_check_depth(struct sto_tree_node *node);
struct sto_tree_params *sto_tree_params(struct sto_tree_node *node);

int sto_tree_add_inode(struct sto_tree_node *parent_node, struct sto_inode *inode);

#endif /* _STO_TREE_H_ */
