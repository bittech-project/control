#include <spdk/stdinc.h>
#include <spdk/likely.h>
#include <spdk/log.h>
#include <spdk/string.h>
#include <spdk/json.h>

#include "sto_async.h"
#include "sto_rpc_aio.h"
#include "sto_tree.h"
#include "sto_inode.h"
#include "sto_json.h"

#include "scst.h"

struct spdk_json_write_ctx;

static char *
scst_available_attrs_line(char **lines, const char *prefix)
{
	int i, prefix_len;

	prefix_len = strlen(prefix);

	for (i = 0; lines[i] != NULL; i++) {
		if (!strncmp(prefix, lines[i], prefix_len)) {
			return lines[i] + prefix_len + 1;
		}
	}

	return NULL;
}

static void
scst_available_attrs(char **available_attrs)
{
	int i;

	for (i = 0; available_attrs[i] != NULL; i++) {
		char *attr = available_attrs[i];
		int attr_len = strlen(attr);

		if (attr[attr_len - 1] == ',') {
			attr[attr_len - 1] = '\0';
		}
	}

	return;
}

static char **
scst_available_attrs_create(const char *buf, const char *prefix)
{
	char **lines;
	char **available_attrs = NULL, *available_attrs_line;

	if (spdk_unlikely(!prefix || !buf)) {
		SPDK_ERRLOG("Buf or Prefix is NULL!\n");
		return NULL;
	}

	lines = spdk_strarray_from_string(buf, "\n");
	if (spdk_unlikely(!lines)) {
		SPDK_ERRLOG("Failed to split scst attr filter\n");
		return NULL;
	}

	available_attrs_line = scst_available_attrs_line(lines, prefix);
	if (!available_attrs_line) {
		SPDK_ERRLOG("Failed to find available attrs line\n");
		goto out;
	}

	available_attrs = spdk_strarray_from_string(available_attrs_line, " ");
	if (spdk_unlikely(!available_attrs)) {
		SPDK_ERRLOG("Failed to split scst attr line\n");
		goto out;
	}

	scst_available_attrs(available_attrs);

out:
	spdk_strarray_free(lines);

	return available_attrs;
}

struct read_available_attrs_ctx {
	const char *prefix;

	void *cb_arg;
	sto_generic_cb cb_fn;

	char ***available_attrs;
};

static void
read_available_attrs_done(void *priv, char *buf, int rc)
{
	struct read_available_attrs_ctx *ctx = priv;

	if (spdk_unlikely(rc)) {
		goto out;
	}

	*ctx->available_attrs = scst_available_attrs_create(buf, ctx->prefix);
	if (spdk_unlikely(!*ctx->available_attrs)) {
		rc = -ENOMEM;
		goto out;
	}

	scst_available_attrs_print(*ctx->available_attrs);

out:
	ctx->cb_fn(ctx->cb_arg, rc);
	free(ctx);

	free(buf);
}

void
scst_read_available_attrs(const char *mgmt_path, const char *prefix,
			  sto_generic_cb cb_fn, void *cb_arg, char ***available_attrs)
{
	struct read_available_attrs_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc config read available_attrs ctx\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	ctx->cb_fn = cb_fn;
	ctx->cb_arg = cb_arg;
	ctx->prefix = prefix;
	ctx->available_attrs = available_attrs;

	sto_rpc_readfile(mgmt_path, 0, read_available_attrs_done, ctx);
}

void
scst_available_attrs_print(char **available_attrs)
{
	int i;

	if (!available_attrs) {
		return;
	}

	SPDK_ERRLOG("GLEB: Print available attrs:");

	for (i = 0; available_attrs[i] != NULL; i++) {
		printf(" %s,", available_attrs[i]);
	}

	printf("\n");
}

bool
scst_available_attrs_find(char **available_attrs, char *attr)
{
	int i;

	for (i = 0; available_attrs[i] != NULL; i++) {
		if (!strcmp(available_attrs[i], attr)) {
			return true;
		}
	}

	return false;
}

void
scst_available_attrs_destroy(char **available_attrs)
{
	spdk_strarray_free(available_attrs);
}

static char *
scst_attr(const char *buf, bool nonkey)
{
	char *attr = NULL;
	char **lines;

	if (spdk_unlikely(!buf)) {
		return NULL;
	}

	lines = spdk_strarray_from_string(buf, "\n");
	if (spdk_unlikely(!lines)) {
		SPDK_ERRLOG("Failed to split scst attr\n");
		return NULL;
	}

	if (!nonkey && (!lines[1] || strcmp(lines[1], "[key]"))) {
		goto out;
	}

	attr = strdup(lines[0]);

out:
	spdk_strarray_free(lines);

	return attr;
}

static void
scst_serialize_attr(struct sto_inode *attr_inode, struct spdk_json_write_ctx *w)
{
	char *attr;

	if (sto_inode_read_only(attr_inode)) {
		return;
	}

	attr = scst_attr(sto_file_inode_buf(attr_inode), false);
	if (!attr) {
		return;
	}

	spdk_json_write_named_string(w, attr_inode->name, attr);

	free(attr);
}

void
scst_serialize_attrs(struct sto_tree_node *obj_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *attr_node;

	STO_TREE_FOREACH_TYPE(attr_node, obj_node, STO_INODE_TYPE_FILE) {
		struct sto_inode *inode = attr_node->inode;

		scst_serialize_attr(inode, w);
	}
}

static int
scst_write_attrs_cb(void *cb_ctx, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *attrs_node = cb_ctx;

	spdk_json_write_object_begin(w);

	scst_serialize_attrs(attrs_node, w);

	spdk_json_write_object_end(w);

	return 0;
}

struct read_attrs_ctx {
	struct sto_json_ctx *json;

	void *cb_arg;
	sto_generic_cb cb_fn;
};

static void
read_attrs_done(void *cb_arg, struct sto_tree_node *tree_root, int rc)
{
	struct read_attrs_ctx *ctx = cb_arg;

	if (spdk_unlikely(rc)) {
		goto out;
	}

	rc = sto_json_ctx_write(ctx->json, true, scst_write_attrs_cb, (void *) tree_root);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to dump SCST dev attributes\n");
		goto out;
	}

	sto_json_print("Print SCST attributes", ctx->json->values);

out:
	ctx->cb_fn(ctx->cb_arg, rc);
	free(ctx);

	sto_tree_free(tree_root);
}

void
scst_read_attrs(const char *dirpath, sto_generic_cb cb_fn, void *cb_arg, struct sto_json_ctx *json)
{
	struct read_attrs_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc dev read attrs ctx\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	ctx->cb_fn = cb_fn;
	ctx->cb_arg = cb_arg;
	ctx->json = json;

	sto_tree(dirpath, 1, false, read_attrs_done, ctx);
}
