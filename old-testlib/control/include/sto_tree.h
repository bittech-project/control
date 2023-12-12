#ifndef _STO_TREE_H_
#define _STO_TREE_H_

#include <spdk/queue.h>

#include "sto_async.h"
#include "sto_inode.h"

struct sto_tree_node;

typedef void (*sto_tree_info_json_t)(struct sto_tree_node *tree_root,
				     struct spdk_json_write_ctx *w);

struct sto_tree_node {
	void *priv;

	struct sto_tree_node *parent;
	struct sto_tree_node *cur_child;

	struct sto_inode *inode;

	uint32_t level;

	TAILQ_ENTRY(sto_tree_node) list;
	TAILQ_HEAD(, sto_tree_node) childs;
};

struct sto_tree_params {
	uint32_t depth;
	bool only_dirs;
};

typedef void (*sto_tree_complete)(void *cb_arg, struct sto_tree_node *tree_root, int rc);

void sto_tree(const char *dirpath, uint32_t depth, bool only_dirs,
	      sto_tree_complete cb_fn, void *cb_arg);

typedef void (*sto_tree_buf_complete)(void *cb_arg, int rc);

void sto_tree_buf(const char *dirpath, uint32_t depth, bool only_dirs,
		  sto_tree_buf_complete cb_fn, void *cb_arg,
		  struct sto_tree_node *tree_root);

void sto_tree_free(struct sto_tree_node *tree_root);

void sto_tree_get_ref(struct sto_tree_node *node);
void sto_tree_put_ref(struct sto_tree_node *node);
void sto_tree_set_error(struct sto_tree_node *node, int rc);
bool sto_tree_check_error(struct sto_tree_node *node);
bool sto_tree_check_depth(struct sto_tree_node *node);
struct sto_tree_params *sto_tree_params(struct sto_tree_node *node);

int sto_tree_add_inode(struct sto_tree_node *parent_node, struct sto_inode *inode);
struct sto_tree_node *sto_tree_node_find(struct sto_tree_node *node, const char *path);
struct sto_tree_node *sto_tree_node_resolv_lnk(struct sto_tree_node *lnk_node);

static inline struct sto_tree_node *
sto_tree_node_next_child(struct sto_tree_node *child, struct sto_tree_node *parent)
{
	return !child ? TAILQ_FIRST(&parent->childs) : TAILQ_NEXT(child, list);
}

static inline struct sto_tree_node *
sto_tree_node_first_child(struct sto_tree_node *parent)
{
	return sto_tree_node_next_child(NULL, parent);
}

static inline struct sto_tree_node *
sto_tree_node_next_child_type(struct sto_tree_node *child,
			      struct sto_tree_node *parent, enum sto_inode_type type)
{
	struct sto_tree_node *node;

	node = sto_tree_node_next_child(child, parent);
	if (!node) {
		return NULL;
	}

	TAILQ_FOREACH_FROM(node, &parent->childs, list) {
		if (node->inode->type == type) {
			return node;
		}
	}

	return NULL;
}

static inline struct sto_tree_node *
sto_tree_node_first_child_type(struct sto_tree_node *parent, enum sto_inode_type type)
{
	return sto_tree_node_next_child_type(NULL, parent, type);
}

#define STO_TREE_FOREACH_TYPE(node, root, inode_type)					\
	for ((node) = sto_tree_node_first_child_type((root), inode_type);		\
	     (node);									\
	     (node) = sto_tree_node_next_child_type((node), (root), inode_type))


void sto_tree_info_json(struct sto_tree_node *tree_root, struct spdk_json_write_ctx *w);

#endif /* _STO_TREE_H_ */
