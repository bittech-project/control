#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_tree.h"

static void sto_tree_req_free(struct sto_tree_req *req);

static struct sto_inode *
sto_tree_req_get_root(struct sto_tree_req *req)
{
	struct sto_tree_result *result = req->result;
	return &result->tree_root;
}

static struct sto_tree_req *
to_sto_tree_req(struct sto_inode *inode)
{
	struct sto_inode *root = inode->root;
	struct sto_tree_result *result;

	result = SPDK_CONTAINEROF(root, struct sto_tree_result, tree_root);

	return result->inner.req;
}

static void
sto_tree_get_ref(struct sto_inode *inode)
{
	struct sto_tree_req *req = to_sto_tree_req(inode);
	req->refcnt++;
}

static void
sto_tree_put_ref(struct sto_inode *inode)
{
	struct sto_tree_req *req = to_sto_tree_req(inode);

	assert(req->refcnt > 0);
	if (--req->refcnt == 0) {
		req->tree_done(req->priv);
		sto_tree_req_free(req);
	}
}

static void
sto_tree_set_error(struct sto_inode *inode, int rc)
{
	struct sto_tree_req *req = to_sto_tree_req(inode);
	struct sto_tree_result *result = req->result;

	if (!result->returncode) {
		result->returncode = rc;
	}
}

static int
sto_inode_init(struct sto_inode *inode, struct sto_dirent *dirent,
	       const char *path, struct sto_inode *parent)
{
	int rc = 0;

	memset(inode, 0, sizeof(*inode));

	rc = sto_dirent_copy(dirent, &inode->dirent);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Cann't copy dirent %s\n", dirent->name);
		return -ENOMEM;
	}

	inode->path = strdup(path);
	if (spdk_unlikely(!inode->path)) {
		SPDK_ERRLOG("Cann't allocate memory for inode path: %s\n", path);
		goto free_name;
	}

	if (parent) {
		inode->parent = parent;
		inode->root = parent->root;
		inode->level = parent->level + 1;
	}

	TAILQ_INIT(&inode->childs);

	return 0;

free_name:
	sto_dirent_free(&inode->dirent);

	return -ENOMEM;
}

static struct sto_inode *
sto_inode_alloc(struct sto_inode *parent, struct sto_dirent *dirent)
{
	struct sto_inode *inode;
	char *path;
	int rc;

	inode = rte_zmalloc(NULL, sizeof(*inode), 0);
	if (spdk_unlikely(!inode)) {
		SPDK_ERRLOG("Failed to alloc inode\n");
		return NULL;
	}

	path = spdk_sprintf_alloc("%s/%s", parent->path, dirent->name);
	if (spdk_unlikely(!path)) {
		SPDK_ERRLOG("Failed to create path for a new inode\n");
		goto free_inode;
	}

	rc = sto_inode_init(inode, dirent, path, parent);

	free(path);

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

static void
sto_subtree_free(struct sto_inode *tree_root)
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

static int
sto_subtree_alloc(struct sto_inode *inode)
{
	struct sto_readdir_result *info = &inode->info;
	struct sto_dirents *dirents = &info->dirents;
	int i;

	for (i = 0; i < dirents->cnt; i++) {
		struct sto_dirent *dirent = &dirents->dirents[i];
		struct sto_inode *child;

		if ((dirent->mode & S_IFMT) != S_IFDIR) {
			continue;
		}

		child = sto_inode_alloc(inode, dirent);
		if (spdk_unlikely(!child)) {
			SPDK_ERRLOG("Failed to alloc child inode\n");
			return -ENOMEM;
		}

		TAILQ_INSERT_TAIL(&inode->childs, child, list);
	}

	return 0;
}

static void
sto_tree_read_done(void *priv)
{
	struct sto_inode *inode = priv;
	struct sto_readdir_result *info = &inode->info;
	struct sto_tree_req *req = to_sto_tree_req(inode);
	int rc;

	rc = info->returncode;

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to readdir\n");
		goto out_err;
	}

	if (spdk_unlikely(req->result->returncode)) {
		SPDK_ERRLOG("Some readdir request failed, go out\n");
		goto out;
	}

	if (req->depth && inode->level == req->depth) {
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

static struct sto_tree_req *
sto_tree_req_alloc(const char *dirpath)
{
	struct sto_tree_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for STO tree req\n");
		return NULL;
	}

	req->dirpath = strdup(dirpath);
	if (spdk_unlikely(!req->dirpath)) {
		SPDK_ERRLOG("Cann't allocate memory for dirpath: %s\n", dirpath);
		goto free_req;
	}

	return req;

free_req:
	rte_free(req);

	return NULL;
}

static void
sto_tree_req_init_cb(struct sto_tree_req *req, tree_done_t tree_done, void *priv)
{
	req->tree_done = tree_done;
	req->priv = priv;
}

static void
sto_tree_req_free(struct sto_tree_req *req)
{
	free((char *) req->dirpath);
	rte_free(req);
}

static void
sto_tree_req_submit(struct sto_tree_req *req)
{
	struct sto_inode *tree_root = sto_tree_req_get_root(req);
	sto_tree_read(tree_root);
}

static int sto_tree_result_init(struct sto_tree_result *result, struct sto_tree_req *req);

int
sto_tree(const char *dirpath, uint32_t depth, struct sto_tree_args *args)
{
	struct sto_tree_req *req;
	int rc;

	req = sto_tree_req_alloc(dirpath);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc memory for tree req\n");
		return -ENOMEM;
	}

	rc = sto_tree_result_init(args->result, req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init tree result\n");
		sto_tree_req_free(req);
		return rc;
	}

	req->depth = depth;
	sto_tree_req_init_cb(req, args->tree_done, args->priv);

	sto_tree_req_submit(req);

	return 0;
}

static int
sto_tree_result_init(struct sto_tree_result *result, struct sto_tree_req *req)
{
	struct sto_dirent dirent = {"root", 0};
	struct sto_inode *tree_root;
	int rc;

	tree_root = &result->tree_root;

	rc = sto_inode_init(tree_root, &dirent, req->dirpath, NULL);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init root inode\n");
		return rc;
	}

	tree_root->root = tree_root;
	tree_root->level = 1;

	result->inner.req = req;
	req->result = result;

	return 0;
}

void sto_tree_result_free(struct sto_tree_result *result)
{
	sto_subtree_free(&result->tree_root);
}
