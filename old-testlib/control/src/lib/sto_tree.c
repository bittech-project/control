#include "sto_tree.h"

#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>
#include <spdk/queue.h>

#include "sto_inode.h"

struct spdk_json_write_ctx;

enum tree_type {
	TREE_TYPE_NONE,
	TREE_TYPE_BASIC,
	TREE_TYPE_WITH_BUF,
};

struct tree_cpl {
	void *cb_arg;

	enum tree_type type;
	union {
		struct {
			sto_tree_complete cb_fn;
			struct sto_tree_node tree_root;
		} basic;

		struct {
			sto_tree_buf_complete cb_fn;
			struct sto_tree_node *tree_root;
		} with_buf;
	} u;
};

static void
tree_call_cpl(struct tree_cpl *cpl, int rc)
{
	switch (cpl->type) {
	case TREE_TYPE_BASIC:
		cpl->u.basic.cb_fn(cpl->cb_arg, &cpl->u.basic.tree_root, rc);
		break;
	case TREE_TYPE_WITH_BUF:
		cpl->u.with_buf.cb_fn(cpl->cb_arg, rc);
		break;
	default:
		assert(0);
	};
}

struct sto_tree_cmd {
	struct sto_tree_node *tree_root;
	struct sto_tree_params params;
	struct tree_cpl cpl;

	int returncode;
	uint32_t refcnt;
};

struct sto_tree_cmd_opts {
	const char *dirpath;
	uint32_t depth;
	bool only_dirs;
};

static int sto_tree_init(struct sto_tree_node *tree_root, const char *dirpath);

static int
tree_cmd_init(struct sto_tree_cmd *cmd, struct sto_tree_cmd_opts *opts)
{
	struct tree_cpl *cpl = &cmd->cpl;
	struct sto_tree_node *tree_root;
	int rc;

	switch (cpl->type) {
	case TREE_TYPE_BASIC:
		tree_root = &cpl->u.basic.tree_root;
		break;
	case TREE_TYPE_WITH_BUF:
		tree_root = cpl->u.with_buf.tree_root;
		break;
	default:
		SPDK_ERRLOG("Got unsupported readfile type (%d)\n", cpl->type);
		return -EINVAL;
	};

	rc = sto_tree_init(tree_root, opts->dirpath);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init root node\n");
		return rc;
	}

	cmd->tree_root = tree_root;
	tree_root->priv = cmd;

	cmd->params.depth = opts->depth;
	cmd->params.only_dirs = opts->only_dirs;

	return 0;
}

static struct sto_tree_cmd *
sto_tree_cmd_alloc(struct sto_tree_cmd_opts *opts, struct tree_cpl *cpl)
{
	struct sto_tree_cmd *cmd;
	int rc;

	cmd = calloc(1, sizeof(*cmd));
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for STO tree cmd\n");
		return NULL;
	}

	cmd->cpl = *cpl;

	rc = tree_cmd_init(cmd, opts);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Faild to init tree cmd\n");
		goto free_cmd;
	}

	return cmd;

free_cmd:
	free(cmd);

	return NULL;
}

static void
sto_tree_cmd_free(struct sto_tree_cmd *cmd)
{
	cmd->tree_root->priv = NULL;
	free(cmd);
}

static void
sto_tree_cmd_run(struct sto_tree_cmd *cmd)
{
	struct sto_tree_node *tree_root = cmd->tree_root;
	sto_inode_read(tree_root->inode);
}

static struct sto_tree_cmd *
sto_tree_cmd(struct sto_tree_node *node)
{
	return node->priv;
}

void
sto_tree_get_ref(struct sto_tree_node *node)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(node);
	cmd->refcnt++;
}

void
sto_tree_put_ref(struct sto_tree_node *node)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(node);

	assert(cmd->refcnt > 0);
	if (--cmd->refcnt == 0) {
		tree_call_cpl(&cmd->cpl, cmd->returncode);
		sto_tree_cmd_free(cmd);
	}
}

void
sto_tree_set_error(struct sto_tree_node *node, int rc)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(node);

	if (!cmd->returncode) {
		cmd->returncode = rc;
	}
}

bool
sto_tree_check_error(struct sto_tree_node *node)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(node);
	return cmd->returncode;
}

bool
sto_tree_check_depth(struct sto_tree_node *node)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(node);
	uint32_t depth = cmd->params.depth;

	if (depth && node->level == depth) {
		return true;
	}

	return false;
}

struct sto_tree_params *
sto_tree_params(struct sto_tree_node *node)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(node);
	return &cmd->params;
}

static void
sto_tree_node_link_parent(struct sto_tree_node *node, struct sto_tree_node *parent)
{
	node->parent = parent;
	node->level = parent->level + 1;

	TAILQ_INSERT_TAIL(&parent->childs, node, list);
}

static void
sto_tree_node_add_inode(struct sto_tree_node *node, struct sto_inode *inode)
{
	node->inode = inode;
	inode->node = node;
}

static struct sto_tree_node *
sto_tree_node_alloc(void)
{
	struct sto_tree_node *node;

	node = calloc(1, sizeof(*node));
	if (spdk_unlikely(!node)) {
		SPDK_ERRLOG("Failed to alloc node\n");
		return NULL;
	}

	TAILQ_INIT(&node->childs);

	return node;
}

int
sto_tree_add_inode(struct sto_tree_node *parent_node, struct sto_inode *inode)
{
	struct sto_tree_node *node;

	node = sto_tree_node_alloc();
	if (spdk_unlikely(!node)) {
		SPDK_ERRLOG("Failed to alloc node\n");
		return -ENOMEM;
	}

	sto_tree_node_add_inode(node, inode);
	sto_tree_node_link_parent(node, parent_node);

	node->priv = parent_node->priv;

	return 0;
}

struct sto_tree_node *
sto_tree_node_find(struct sto_tree_node *root, const char *path)
{
	struct sto_tree_node *node;

	TAILQ_FOREACH(node, &root->childs, list) {
		if (!strcmp(node->inode->name, path)) {
			return node;
		}
	}

	return NULL;
}

struct sto_tree_node *
sto_tree_node_resolv_lnk(struct sto_tree_node *lnk_node)
{
	struct sto_tree_node *node = NULL;
	char **tokens;
	int i;

	tokens = spdk_strarray_from_string(sto_file_inode_buf(lnk_node->inode), "/");
	if (spdk_unlikely(!tokens)) {
		SPDK_ERRLOG("Failed to split link\n");
		return NULL;
	}

	node = lnk_node->parent;

	for (i = 0; tokens[i] != NULL && node; i++) {
		SPDK_ERRLOG("GLEB: node[%s], token[%s]\n", node->inode->name, tokens[i]);

		if (!strcmp(tokens[i], "..")) {
			node = node->parent;
			continue;
		}

		node = sto_tree_node_find(node, tokens[i]);
	}

	spdk_strarray_free(tokens);

	return node;
}

static void
__sto_tree_node_free(struct sto_tree_node *node)
{
	struct sto_inode *inode = node->inode;

	if (spdk_likely(inode)) {
		inode->ops->destroy(inode);
		node->inode = NULL;
	}
}

static void
sto_tree_node_free(struct sto_tree_node *node)
{
	__sto_tree_node_free(node);
	free(node);
}

static int
sto_tree_init(struct sto_tree_node *tree_root, const char *dirpath)
{
	struct sto_inode *inode;
	uint32_t mode = S_IRWXU | S_IFDIR;

	memset(tree_root, 0, sizeof(*tree_root));

	inode = sto_inode_create("root", dirpath, mode);
	if (spdk_unlikely(!inode)) {
		SPDK_ERRLOG("Cann't allocate memory for root node\n");
		return -ENOMEM;
	}

	sto_tree_node_add_inode(tree_root, inode);

	tree_root->level = 0;

	TAILQ_INIT(&tree_root->childs);

	return 0;
}

void
sto_tree_free(struct sto_tree_node *tree_root)
{
	struct sto_tree_node *parent = tree_root;
	struct sto_tree_node *next_node;

	while (parent != NULL) {
		if (!TAILQ_EMPTY(&parent->childs)) {
			next_node = TAILQ_FIRST(&parent->childs);
			TAILQ_REMOVE(&parent->childs, next_node, list);
		} else {
			next_node = parent->parent;

			if (parent == tree_root) {
				__sto_tree_node_free(parent);
			} else {
				sto_tree_node_free(parent);
			}
		}

		parent = next_node;
	}
}

static void
sto_subtree_info_json(struct sto_tree_node *parent, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *node, *next_node = NULL;

	for (node = parent; node != parent->parent; node = next_node) {
		if (TAILQ_EMPTY(&node->childs)) {
			struct sto_inode *inode = node->inode;
			inode->ops->info_json(inode, w);

			next_node = node->parent;
			continue;
		}

		node->cur_child = sto_tree_node_next_child(node->cur_child, node);

		if (!node->cur_child) {
			spdk_json_write_array_end(w);
			spdk_json_write_object_end(w);

			next_node = node->parent;
			continue;
		}

		if (node->cur_child == TAILQ_FIRST(&node->childs)) {
			spdk_json_write_object_begin(w);
			spdk_json_write_named_array_begin(w, node->inode->name);
		}

		next_node = node->cur_child;
	}
}

void
sto_tree_info_json(struct sto_tree_node *tree_root, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *node;

	spdk_json_write_array_begin(w);

	TAILQ_FOREACH(node, &tree_root->childs, list) {
		sto_subtree_info_json(node, w);
	}

	spdk_json_write_array_end(w);
}

static int
tree(struct sto_tree_cmd_opts *opts, struct tree_cpl *cpl)
{
	struct sto_tree_cmd *cmd;

	cmd = sto_tree_cmd_alloc(opts, cpl);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to alloc memory for tree cmd\n");
		return -ENOMEM;
	}

	sto_tree_cmd_run(cmd);

	return 0;
}

void
sto_tree(const char *dirpath, uint32_t depth, bool only_dirs,
	 sto_tree_complete cb_fn, void *cb_arg)
{
	struct tree_cpl cpl = {};
	struct sto_tree_cmd_opts opts = {
		.dirpath = dirpath,
		.depth = depth,
		.only_dirs = only_dirs,
	};
	int rc;

	cpl.type = TREE_TYPE_BASIC;
	cpl.u.basic.cb_fn = cb_fn;
	cpl.cb_arg = cb_arg;

	rc = tree(&opts, &cpl);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("tree() failed\n");
		tree_call_cpl(&cpl, rc);
		return;
	}

	return;
}

void
sto_tree_buf(const char *dirpath, uint32_t depth, bool only_dirs,
	     sto_tree_buf_complete cb_fn, void *cb_arg,
	     struct sto_tree_node *tree_root)
{
	struct tree_cpl cpl = {};
	struct sto_tree_cmd_opts opts = {
		.dirpath = dirpath,
		.depth = depth,
		.only_dirs = only_dirs,
	};
	int rc;

	cpl.type = TREE_TYPE_WITH_BUF;
	cpl.u.with_buf.cb_fn = cb_fn;
	cpl.cb_arg = cb_arg;
	cpl.u.with_buf.tree_root = tree_root;

	rc = tree(&opts, &cpl);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("tree() failed\n");
		tree_call_cpl(&cpl, rc);
		return;
	}

	return;
}
