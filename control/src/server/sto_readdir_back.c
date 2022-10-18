#include <spdk/likely.h>

#include "sto_readdir_back.h"

static int sto_readdir_exec(void *arg);
static void sto_readdir_exec_done(void *arg, int rc);

static struct sto_exec_ops readdir_ops = {
	.name = "readdir",
	.exec = sto_readdir_exec,
	.exec_done = sto_readdir_exec_done,
};

static void
sto_readdir_exec_done(void *arg, int rc)
{
	struct sto_readdir_back_ctx *ctx = arg;

	ctx->returncode = rc;
	ctx->readdir_back_done(ctx);
}

static struct sto_dirent *
sto_readdir_alloc_dirent(const char *d_name)
{
	struct sto_dirent *d;

	d = calloc(1, sizeof(*d));
	if (spdk_unlikely(!d)) {
		printf("server: Failed to alloc dirent\n");
		return NULL;
	}

	d->name = strdup(d_name);
	if (spdk_unlikely(!d->name)) {
		printf("server: Failed to alloc dirent name\n");
		goto free_dirent;
	}

	return d;

free_dirent:
	free(d);

	return NULL;
}

static void
sto_readdir_dirents_free(struct sto_readdir_back_ctx *ctx)
{
	struct sto_dirent *d, *tmp;

	TAILQ_FOREACH_SAFE(d, &ctx->dirent_list, list, tmp) {
		TAILQ_REMOVE(&ctx->dirent_list, d, list);

		free(d->name);
		free(d);
	}

	ctx->dirent_cnt = 0;
}

static int
sto_readdir_exec(void *arg)
{
	struct sto_readdir_back_ctx *ctx = arg;
	struct dirent *entry;
	DIR *dir;
	int rc = 0;

	dir = opendir(ctx->dirname);
	if (spdk_unlikely(!dir)) {
		printf("server: Failed to open %s dir\n", ctx->dirname);
		return -errno;
	}

	entry = readdir(dir);

	while (entry != NULL) {
		struct sto_dirent *d;

		d = sto_readdir_alloc_dirent(entry->d_name);
		if (spdk_unlikely(!d)) {
			sto_readdir_dirents_free(ctx);
			printf("server: Failed to alloc dirent\n");
			break;
		}

		TAILQ_INSERT_TAIL(&ctx->dirent_list, d, list);
		ctx->dirent_cnt++;

		entry = readdir(dir);
	}

	rc = closedir(dir);
	if (spdk_unlikely(rc == -1)) {
		printf("server: Failed to close %s dir\n", ctx->dirname);
	}

	return rc;
}

static struct sto_readdir_back_ctx *
sto_readdir_back_alloc(const char *dirname)
{
	struct sto_readdir_back_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		printf("Cann't allocate memory for STO readdir ctx\n");
		return NULL;
	}

	ctx->dirname = strdup(dirname);
	if (spdk_unlikely(!ctx->dirname)) {
		printf("Cann't allocate memory for dirname: %s\n", dirname);
		goto free_ctx;
	}

	sto_exec_init_ctx(&ctx->exec_ctx, &readdir_ops, ctx);

	TAILQ_INIT(&ctx->dirent_list);

	return ctx;

free_ctx:
	free(ctx);

	return NULL;
}

static void
sto_readdir_back_init_cb(struct sto_readdir_back_ctx *ctx,
			 readdir_back_done_t readdir_back_done, void *priv)
{
	ctx->readdir_back_done = readdir_back_done;
	ctx->priv = priv;
}

void
sto_readdir_back_free(struct sto_readdir_back_ctx *ctx)
{
	sto_readdir_dirents_free(ctx);

	free((char *) ctx->dirname);
	free(ctx);
}

static int
sto_readdir_back_submit(struct sto_readdir_back_ctx *ctx)
{
	return sto_exec(&ctx->exec_ctx);
}

int
sto_readdir_back(const char *dirname, readdir_back_done_t readdir_back_done, void *priv)
{
	struct sto_readdir_back_ctx *ctx;
	int rc;

	ctx = sto_readdir_back_alloc(dirname);
	if (spdk_unlikely(!ctx)) {
		printf("server: Failed to alloc memory for back readdir\n");
		return -ENOMEM;
	}

	sto_readdir_back_init_cb(ctx, readdir_back_done, priv);

	rc = sto_readdir_back_submit(ctx);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit back readdir, rc=%d\n", rc);
		goto free_readdir;
	}

	return 0;

free_readdir:
	sto_readdir_back_free(ctx);

	return rc;
}
