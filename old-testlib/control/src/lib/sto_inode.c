#include "sto_inode.h"

#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/string.h>

#include "sto_rpc_aio.h"
#include "sto_tree.h"
#include "sto_rpc_readdir.h"

struct spdk_json_write_ctx;

static struct sto_inode *sto_file_inode_create(void);
static struct sto_inode *sto_dir_inode_create(void);
static struct sto_inode *sto_lnk_inode_create(void);

static enum sto_inode_type
sto_inode_type(uint32_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
		return STO_INODE_TYPE_FILE;
	case S_IFDIR:
		return STO_INODE_TYPE_DIR;
	case S_IFLNK:
		return STO_INODE_TYPE_LNK;
	default:
		return STO_INODE_TYPE_UNSUPPORTED;
	}
}

static void
sto_inode_get_ref(struct sto_inode *inode)
{
	sto_tree_get_ref(inode->node);
}

static void
sto_inode_put_ref(struct sto_inode *inode)
{
	sto_tree_put_ref(inode->node);
}

static void
sto_inode_set_error(struct sto_inode *inode, int rc)
{
	sto_tree_set_error(inode->node, rc);
}

static bool
sto_inode_check_error(struct sto_inode *inode)
{
	return sto_tree_check_error(inode->node);
}

static bool
sto_inode_check_tree_depth(struct sto_inode *inode)
{
	return sto_tree_check_depth(inode->node);
}

struct sto_inode *
sto_inode_create(const char *name, const char *path, uint32_t mode, ...)
{
	struct sto_inode *inode;
	enum sto_inode_type type = sto_inode_type(mode);
	va_list args;

	switch (type) {
	case STO_INODE_TYPE_FILE:
		inode = sto_file_inode_create();
		break;
	case STO_INODE_TYPE_DIR:
		inode = sto_dir_inode_create();
		break;
	case STO_INODE_TYPE_LNK:
		inode = sto_lnk_inode_create();
		break;
	default:
		SPDK_ERRLOG("Unsupported %d inode type\n", type);
		return NULL;
	}

	inode->name = strdup(name);
	if (spdk_unlikely(!inode->name)) {
		SPDK_ERRLOG("Cann't allocate memory for inode name\n");
		goto free_inode;
	}

	va_start(args, mode);
	inode->path = spdk_vsprintf_alloc(path, args);
	va_end(args);

	if (spdk_unlikely(!inode->path)) {
		SPDK_ERRLOG("Cann't allocate memory for inode path\n");
		goto free_name;
	}

	inode->type = type;
	inode->mode = mode;

	return inode;

free_name:
	free((char *) inode->name);

free_inode:
	inode->ops->destroy(inode);

	return NULL;
}

static void
sto_inode_free(struct sto_inode *inode)
{
	free((char *) inode->name);
	free(inode->path);
}

static void
sto_inode_read_done(void *priv, int rc)
{
	struct sto_inode *inode = priv;

	if (spdk_unlikely(sto_inode_check_error(inode))) {
		SPDK_ERRLOG("Some inode failed, go out\n");
		goto out;
	}

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to read tree, rc=%d\n", rc);
		goto out_err;
	}

	rc = inode->ops->read_done(inode);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to alloc childs\n");
		goto out_err;
	}

out:
	sto_inode_put_ref(inode);

	return;

out_err:
	sto_inode_set_error(inode, rc);
	goto out;
}

void
sto_inode_read(struct sto_inode *inode)
{
	int rc;

	if (sto_inode_write_only(inode)) {
		return;
	}

	sto_inode_get_ref(inode);

	rc = inode->ops->read(inode);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to read %s inode\n", inode->path);
		sto_inode_set_error(inode, rc);
		sto_inode_put_ref(inode);
	}
}

static int
sto_file_inode_read(struct sto_inode *inode)
{
	struct sto_file_inode *file_inode = sto_file_inode(inode);

	sto_rpc_readfile_buf(inode->path, 0,
			     sto_inode_read_done, inode,
			     &file_inode->buf);

	return 0;
}

static int
sto_file_inode_read_done(struct sto_inode *inode)
{
	return 0;
}

static void
sto_file_inode_info_json(struct sto_inode *inode, struct spdk_json_write_ctx *w)
{
	struct sto_file_inode *file_inode = sto_file_inode(inode);

	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, inode->name, file_inode->buf);
	spdk_json_write_object_end(w);
}

static void
sto_file_inode_destroy(struct sto_inode *inode)
{
	struct sto_file_inode *file_inode = sto_file_inode(inode);

	sto_inode_free(inode);

	free(file_inode->buf);
	free(file_inode);
}

static struct sto_inode_ops sto_file_inode_ops = {
	.read = sto_file_inode_read,
	.read_done = sto_file_inode_read_done,
	.info_json = sto_file_inode_info_json,
	.destroy = sto_file_inode_destroy,
};

static struct sto_inode *
sto_file_inode_create(void)
{
	struct sto_file_inode *file_inode;
	struct sto_inode *inode;

	file_inode = calloc(1, sizeof(*file_inode));
	if (spdk_unlikely(!file_inode)) {
		SPDK_ERRLOG("Failed to create file node\n");
		return NULL;
	}

	inode = &file_inode->inode;
	inode->ops = &sto_file_inode_ops;

	return inode;
}

static int
sto_dir_inode_parse(struct sto_dirent *dirent, struct sto_tree_node *parent_node)
{
	struct sto_inode *inode;
	struct sto_tree_params *tree_params = sto_tree_params(parent_node);
	int rc = 0;

	if (tree_params->only_dirs && sto_inode_type(dirent->mode) != STO_INODE_TYPE_DIR) {
		return 0;
	}

	inode = sto_inode_create(dirent->name, "%s/%s",
				 dirent->mode, parent_node->inode->path, dirent->name);
	if (spdk_unlikely(!inode)) {
		SPDK_ERRLOG("Failed to allod inode\n");
		return -ENOMEM;
	}

	rc = sto_tree_add_inode(parent_node, inode);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to add inode, rc=%d\n", rc);
		goto free_inode;
	}

	return 0;

free_inode:
	inode->ops->destroy(inode);

	return rc;
}

static int
sto_dir_inode_read(struct sto_inode *inode)
{
	struct sto_dir_inode *dir_inode = sto_dir_inode(inode);

	if (sto_inode_check_tree_depth(inode)) {
		sto_inode_put_ref(inode);
		return 0;
	}

	sto_rpc_readdir(inode->path, sto_inode_read_done, inode, &dir_inode->dirents);

	return 0;
}

static int
sto_dir_inode_read_done(struct sto_inode *inode)
{
	struct sto_dir_inode *dir_inode = sto_dir_inode(inode);
	struct sto_dirents *dirents = &dir_inode->dirents;
	struct sto_tree_node *node, *parent_node;
	size_t i;
	int rc = 0;

	parent_node = inode->node;

	for (i = 0; i < dirents->cnt; i++) {
		struct sto_dirent *dirent = &dirents->dirents[i];

		rc = sto_dir_inode_parse(dirent, parent_node);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to parse dirent\n");
			goto out;
		}
	}

	TAILQ_FOREACH(node, &parent_node->childs, list) {
		sto_inode_read(node->inode);
	}

out:
	sto_dirents_free(dirents);

	return rc;
}

static void
sto_dir_inode_info_json(struct sto_inode *inode, struct spdk_json_write_ctx *w)
{
	spdk_json_write_string(w, inode->name);
}

static void
sto_dir_inode_destroy(struct sto_inode *inode)
{
	struct sto_dir_inode *dir_inode = sto_dir_inode(inode);

	sto_inode_free(inode);

	sto_dirents_free(&dir_inode->dirents);
	free(dir_inode);
}

static struct sto_inode_ops sto_dir_inode_ops = {
	.read = sto_dir_inode_read,
	.read_done = sto_dir_inode_read_done,
	.info_json = sto_dir_inode_info_json,
	.destroy = sto_dir_inode_destroy,
};

static struct sto_inode *
sto_dir_inode_create(void)
{
	struct sto_dir_inode *dir_inode;
	struct sto_inode *inode;

	dir_inode = calloc(1, sizeof(*dir_inode));
	if (spdk_unlikely(!dir_inode)) {
		SPDK_ERRLOG("Failed to create dir node\n");
		return NULL;
	}

	inode = &dir_inode->inode;
	inode->ops = &sto_dir_inode_ops;

	return inode;
}

static int
sto_lnk_inode_read(struct sto_inode *inode)
{
	struct sto_file_inode *file_inode = sto_file_inode(inode);

	sto_rpc_readlink(inode->path, sto_inode_read_done, inode, &file_inode->buf);

	return 0;
}

static struct sto_inode_ops sto_lnk_inode_ops = {
	.read = sto_lnk_inode_read,
	.read_done = sto_file_inode_read_done,
	.info_json = sto_file_inode_info_json,
	.destroy = sto_file_inode_destroy,
};

static struct sto_inode *
sto_lnk_inode_create(void)
{
	struct sto_file_inode *file_inode;
	struct sto_inode *inode;

	file_inode = calloc(1, sizeof(*file_inode));
	if (spdk_unlikely(!file_inode)) {
		SPDK_ERRLOG("Failed to create lnk node\n");
		return NULL;
	}

	inode = &file_inode->inode;
	inode->ops = &sto_lnk_inode_ops;

	return inode;
}
