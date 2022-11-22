#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/json.h>
#include <spdk/util.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_rpc_aio.h"
#include "sto_tree.h"
#include "sto_inode.h"

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
sto_inode_check_depth(struct sto_inode *inode)
{
	return sto_tree_check_depth(inode->node);
}

struct sto_inode *
sto_inode_create(const char *name, const char *path, enum sto_inode_type type, ...)
{
	struct sto_inode *inode;
	va_list args;

	switch(type) {
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

	va_start(args, type);
	inode->path = spdk_vsprintf_alloc(path, args);
	va_end(args);

	if (spdk_unlikely(!inode->path)) {
		SPDK_ERRLOG("Cann't allocate memory for inode path\n");
		goto free_name;
	}

	return inode;

free_name:
	free((char *) inode->name);

free_inode:
	inode->destroy(inode);

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

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to read tree, rc=%d\n", rc);
		goto out_err;
	}

	if (spdk_unlikely(sto_inode_check_error(inode))) {
		SPDK_ERRLOG("Some readdir command failed, go out\n");
		goto out;
	}

	rc = inode->read_done(inode);
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

	sto_inode_get_ref(inode);

	rc = inode->read(inode);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to read %s inode\n", inode->path);
		sto_inode_set_error(inode, rc);
		sto_inode_put_ref(inode);
	}
}

static int
sto_file_inode_read_done(struct sto_inode *inode)
{
	return 0;
}

static int
sto_file_inode_read(struct sto_inode *inode)
{
	struct sto_file_inode *file_inode = sto_file_inode(inode);
	struct sto_rpc_readfile_args args = {
		.priv = inode,
		.done = sto_inode_read_done,
		.buf = &file_inode->buf,
	};

	return sto_rpc_readfile(inode->path, 0, &args);
}

static void
sto_file_inode_json_info(struct sto_inode *inode, struct spdk_json_write_ctx *w)
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
	rte_free(file_inode);
}

static struct sto_inode *
sto_file_inode_create(void)
{
	struct sto_file_inode *file_inode;
	struct sto_inode *inode;

	file_inode = rte_zmalloc(NULL, sizeof(*file_inode), 0);
	if (spdk_unlikely(!file_inode)) {
		SPDK_ERRLOG("Failed to create file node\n");
		return NULL;
	}

	inode = &file_inode->inode;

	inode->read = sto_file_inode_read;
	inode->read_done = sto_file_inode_read_done;
	inode->json_info = sto_file_inode_json_info;
	inode->destroy = sto_file_inode_destroy;

	return inode;
}

static int
sto_dir_inode_parse(struct sto_dirent *dirent, struct sto_tree_node *parent_node)
{
	struct sto_inode *inode;
	int rc = 0;

	inode = sto_inode_create(dirent->name, "%s/%s",
				 sto_inode_type(dirent->mode),
				 parent_node->inode->path, dirent->name);
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
	inode->destroy(inode);

	return rc;
}

static int
sto_dir_inode_read_done(struct sto_inode *inode)
{
	struct sto_dir_inode *dir_inode = sto_dir_inode(inode);
	struct sto_dirents *dirents = &dir_inode->dirents;
	struct sto_tree_node *node, *parent_node;
	int rc = 0, i;

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

static int
sto_dir_inode_read(struct sto_inode *inode)
{
	struct sto_dir_inode *dir_inode = sto_dir_inode(inode);
	struct sto_rpc_readdir_args args = {
		.priv = inode,
		.done = sto_inode_read_done,
		.dirents = &dir_inode->dirents,
	};

	if (sto_inode_check_depth(inode)) {
		sto_inode_put_ref(inode);
		return 0;
	}

	return sto_rpc_readdir(inode->path, &args);
}

static void
sto_dir_inode_json_info(struct sto_inode *inode, struct spdk_json_write_ctx *w)
{
	spdk_json_write_string(w, inode->name);
}

static void
sto_dir_inode_destroy(struct sto_inode *inode)
{
	struct sto_dir_inode *dir_inode = sto_dir_inode(inode);

	sto_inode_free(inode);

	sto_dirents_free(&dir_inode->dirents);
	rte_free(dir_inode);
}

static struct sto_inode *
sto_dir_inode_create(void)
{
	struct sto_dir_inode *dir_inode;
	struct sto_inode *inode;

	dir_inode = rte_zmalloc(NULL, sizeof(*dir_inode), 0);
	if (spdk_unlikely(!dir_inode)) {
		SPDK_ERRLOG("Failed to create dir node\n");
		return NULL;
	}

	inode = &dir_inode->inode;

	inode->read = sto_dir_inode_read;
	inode->read_done = sto_dir_inode_read_done;
	inode->json_info = sto_dir_inode_json_info;
	inode->destroy = sto_dir_inode_destroy;

	return inode;
}

static int
sto_lnk_inode_read_done(struct sto_inode *inode)
{
	return 0;
}

static int
sto_lnk_inode_read(struct sto_inode *inode)
{
	struct sto_lnk_inode *lnk_inode = sto_lnk_inode(inode);
	struct sto_rpc_readfile_args args = {
		.priv = inode,
		.done = sto_inode_read_done,
		.buf = &lnk_inode->buf,
	};

	return sto_rpc_readfile(inode->path, 0, &args);
}

static void
sto_lnk_inode_json_info(struct sto_inode *inode, struct spdk_json_write_ctx *w)
{
	struct sto_lnk_inode *lnk_inode = sto_lnk_inode(inode);

	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, inode->name, lnk_inode->buf);
	spdk_json_write_object_end(w);
}

static void
sto_lnk_inode_destroy(struct sto_inode *inode)
{
	struct sto_lnk_inode *lnk_inode = sto_lnk_inode(inode);

	sto_inode_free(inode);

	free(lnk_inode->buf);
	rte_free(lnk_inode);
}

static struct sto_inode *
sto_lnk_inode_create(void)
{
	struct sto_lnk_inode *lnk_inode;
	struct sto_inode *inode;

	lnk_inode = rte_zmalloc(NULL, sizeof(*lnk_inode), 0);
	if (spdk_unlikely(!lnk_inode)) {
		SPDK_ERRLOG("Failed to create lnk node\n");
		return NULL;
	}

	inode = &lnk_inode->inode;

	inode->read = sto_lnk_inode_read;
	inode->read_done = sto_lnk_inode_read_done;
	inode->json_info = sto_lnk_inode_json_info;
	inode->destroy = sto_lnk_inode_destroy;

	return inode;
}
