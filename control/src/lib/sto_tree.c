#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_tree.h"

static void sto_tree_cmd_free(struct sto_tree_cmd *cmd);

static struct sto_inode *
sto_tree_cmd_get_root(struct sto_tree_cmd *cmd)
{
	struct sto_tree_info *info = cmd->info;
	return &info->tree_root;
}

static struct sto_tree_cmd *
sto_tree_cmd(struct sto_inode *inode)
{
	struct sto_inode *root = inode->root;
	struct sto_tree_info *info;

	info = SPDK_CONTAINEROF(root, struct sto_tree_info, tree_root);

	return info->inner.cmd;
}

static void
sto_tree_get_ref(struct sto_inode *inode)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(inode);
	cmd->refcnt++;
}

static void
sto_tree_put_ref(struct sto_inode *inode)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(inode);

	assert(cmd->refcnt > 0);
	if (--cmd->refcnt == 0) {
		cmd->tree_cmd_done(cmd->priv);
		sto_tree_cmd_free(cmd);
	}
}

static void
sto_tree_set_error(struct sto_inode *inode, int rc)
{
	struct sto_tree_cmd *cmd = sto_tree_cmd(inode);
	struct sto_tree_info *info = cmd->info;

	if (!info->returncode) {
		info->returncode = rc;
	}
}

static int
sto_child_inode_init(struct sto_inode *inode, struct sto_inode *parent,
		     struct sto_dirent *dirent)
{
	int rc = 0;

	rc = sto_dirent_copy(dirent, &inode->dirent);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Cann't copy dirent %s\n", dirent->name);
		return -ENOMEM;
	}

	inode->path = spdk_sprintf_alloc("%s/%s", parent->path, dirent->name);
	if (spdk_unlikely(!inode->path)) {
		SPDK_ERRLOG("Cann't allocate memory for child inode path\n");
		goto free_dirent;
	}

	inode->parent = parent;
	inode->root = parent->root;
	inode->level = parent->level + 1;

	TAILQ_INIT(&inode->childs);

	return 0;

free_dirent:
	sto_dirent_free(&inode->dirent);

	return -ENOMEM;
}

static struct sto_inode *
sto_child_inode_alloc(struct sto_inode *parent, struct sto_dirent *dirent)
{
	struct sto_inode *inode;
	int rc;

	inode = rte_zmalloc(NULL, sizeof(*inode), 0);
	if (spdk_unlikely(!inode)) {
		SPDK_ERRLOG("Failed to alloc inode\n");
		return NULL;
	}

	rc = sto_child_inode_init(inode, parent, dirent);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init inode\n");
		goto free_inode;
	}

	return inode;

free_inode:
	rte_free(inode);

	return NULL;
}

static void
__sto_inode_free(struct sto_inode *inode)
{
	sto_dirent_free(&inode->dirent);
	free(inode->path);

	sto_readdir_result_free(&inode->info);
}

static void
sto_inode_free(struct sto_inode *inode)
{
	__sto_inode_free(inode);
	rte_free(inode);
}

static int
sto_subtree_alloc(struct sto_inode *parent)
{
	struct sto_readdir_result *info = &parent->info;
	struct sto_dirents *dirents = &info->dirents;
	int i;

	for (i = 0; i < dirents->cnt; i++) {
		struct sto_dirent *dirent = &dirents->dirents[i];
		struct sto_inode *child;

		if ((dirent->mode & S_IFMT) != S_IFDIR) {
			continue;
		}

		child = sto_child_inode_alloc(parent, dirent);
		if (spdk_unlikely(!child)) {
			SPDK_ERRLOG("Failed to alloc child inode\n");
			return -ENOMEM;
		}

		TAILQ_INSERT_TAIL(&parent->childs, child, list);
	}

	return 0;
}

static int
sto_tree_init(struct sto_inode *tree_root, const char *path)
{
	memset(tree_root, 0, sizeof(*tree_root));

	tree_root->path = strdup(path);
	if (spdk_unlikely(!tree_root->path)) {
		SPDK_ERRLOG("Cann't allocate memory for tree root path: %s\n", path);
		return -ENOMEM;
	}

	tree_root->root = tree_root;
	tree_root->level = 1;

	TAILQ_INIT(&tree_root->childs);

	return 0;
}

static void
sto_tree_free(struct sto_inode *tree_root)
{
	struct sto_inode *parent = tree_root;
	struct sto_inode *next_node;

	while (parent != NULL) {
		if (!TAILQ_EMPTY(&parent->childs)) {
			next_node = TAILQ_FIRST(&parent->childs);
			TAILQ_REMOVE(&parent->childs, next_node, list);
		} else {
			next_node = parent->parent;

			if (parent == tree_root) {
				__sto_inode_free(parent);
			} else {
				sto_inode_free(parent);
			}
		}

		parent = next_node;
	}
}

static void sto_tree_read(struct sto_inode *inode);

static bool
sto_subtree_read(struct sto_inode *inode)
{
	struct sto_inode *child;

	if (TAILQ_EMPTY(&inode->childs)) {
		return false;
	}

	TAILQ_FOREACH(child, &inode->childs, list) {
		sto_tree_read(child);
	}

	return true;
}

static void
sto_tree_read_done(void *priv)
{
	struct sto_inode *inode = priv;
	struct sto_readdir_result *info = &inode->info;
	struct sto_tree_cmd *cmd = sto_tree_cmd(inode);
	int rc;

	rc = info->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to readdir\n");
		goto out_err;
	}

	if (spdk_unlikely(cmd->info->returncode)) {
		SPDK_ERRLOG("Some readdir command failed, go out\n");
		goto out;
	}

	if (cmd->depth && inode->level == cmd->depth) {
		goto out;
	}

	rc = sto_subtree_alloc(inode);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to alloc childs\n");
		goto out_err;
	}

	if (!sto_subtree_read(inode)) {
		goto out;
	}

out:
	sto_tree_put_ref(inode);

	return;

out_err:
	sto_tree_set_error(inode, rc);
	goto out;
}

static void
sto_tree_read(struct sto_inode *inode)
{
	struct sto_readdir_args args = {
		.priv = inode,
		.readdir_done = sto_tree_read_done,
		.result = &inode->info,
	};
	int rc;

	sto_tree_get_ref(inode);

	rc = sto_readdir(inode->path, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to read %s inode\n", inode->path);
		sto_tree_set_error(inode, rc);
		sto_tree_put_ref(inode);
	}
}

static int
sto_tree_info_init(struct sto_tree_info *info, struct sto_tree_cmd *cmd)
{
	int rc;

	rc = sto_tree_init(&info->tree_root, cmd->dirpath);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init root inode\n");
		return rc;
	}

	info->inner.cmd = cmd;
	cmd->info = info;

	return 0;
}

void sto_tree_info_free(struct sto_tree_info *info)
{
	sto_tree_free(&info->tree_root);
}

static struct sto_tree_cmd *
sto_tree_cmd_alloc(const char *dirpath)
{
	struct sto_tree_cmd *cmd;

	cmd = rte_zmalloc(NULL, sizeof(*cmd), 0);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for STO tree cmd\n");
		return NULL;
	}

	cmd->dirpath = strdup(dirpath);
	if (spdk_unlikely(!cmd->dirpath)) {
		SPDK_ERRLOG("Cann't allocate memory for dirpath: %s\n", dirpath);
		goto free_cmd;
	}

	return cmd;

free_cmd:
	rte_free(cmd);

	return NULL;
}

static void
sto_tree_cmd_init_cb(struct sto_tree_cmd *cmd, tree_cmd_done_t tree_cmd_done, void *priv)
{
	cmd->tree_cmd_done = tree_cmd_done;
	cmd->priv = priv;
}

static void
sto_tree_cmd_free(struct sto_tree_cmd *cmd)
{
	free((char *) cmd->dirpath);
	rte_free(cmd);
}

static void
sto_tree_cmd_run(struct sto_tree_cmd *cmd)
{
	struct sto_inode *tree_root = sto_tree_cmd_get_root(cmd);
	sto_tree_read(tree_root);
}

int
sto_tree(const char *dirpath, uint32_t depth, struct sto_tree_cmd_args *args)
{
	struct sto_tree_cmd *cmd;
	int rc;

	cmd = sto_tree_cmd_alloc(dirpath);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to alloc memory for tree cmd\n");
		return -ENOMEM;
	}

	rc = sto_tree_info_init(args->info, cmd);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init tree info\n");
		sto_tree_cmd_free(cmd);
		return rc;
	}

	cmd->depth = depth;
	sto_tree_cmd_init_cb(cmd, args->tree_cmd_done, args->priv);

	sto_tree_cmd_run(cmd);

	return 0;
}
