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
	struct sto_readdir_back_req *req = arg;

	req->returncode = rc;
	req->readdir_back_done(req);
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
sto_readdir_dirents_free(struct sto_readdir_back_req *req)
{
	struct sto_dirent *d, *tmp;

	TAILQ_FOREACH_SAFE(d, &req->dirent_list, list, tmp) {
		TAILQ_REMOVE(&req->dirent_list, d, list);

		free(d->name);
		free(d);
	}
}

static int
sto_readdir_exec(void *arg)
{
	struct sto_readdir_back_req *req = arg;
	struct dirent *entry;
	DIR *dir;
	int rc = 0;

	dir = opendir(req->dirname);
	if (spdk_unlikely(!dir)) {
		printf("server: Failed to open %s dir\n", req->dirname);
		return -errno;
	}

	entry = readdir(dir);

	while (entry != NULL) {
		struct sto_dirent *d;

		if (req->skip_hidden && entry->d_name[0] == '.') {
			entry = readdir(dir);
			continue;
		}

		d = sto_readdir_alloc_dirent(entry->d_name);
		if (spdk_unlikely(!d)) {
			sto_readdir_dirents_free(req);
			printf("server: Failed to alloc dirent\n");
			break;
		}

		TAILQ_INSERT_TAIL(&req->dirent_list, d, list);

		entry = readdir(dir);
	}

	rc = closedir(dir);
	if (spdk_unlikely(rc == -1)) {
		printf("server: Failed to close %s dir\n", req->dirname);
	}

	return rc;
}

static struct sto_readdir_back_req *
sto_readdir_back_alloc(const char *dirname, bool skip_hidden)
{
	struct sto_readdir_back_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("Cann't allocate memory for STO readdir req\n");
		return NULL;
	}

	req->dirname = strdup(dirname);
	if (spdk_unlikely(!req->dirname)) {
		printf("Cann't allocate memory for dirname: %s\n", dirname);
		goto free_req;
	}

	req->skip_hidden = skip_hidden;

	sto_exec_init_ctx(&req->exec_ctx, &readdir_ops, req);

	TAILQ_INIT(&req->dirent_list);

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_readdir_back_init_cb(struct sto_readdir_back_req *req,
			 readdir_back_done_t readdir_back_done, void *priv)
{
	req->readdir_back_done = readdir_back_done;
	req->priv = priv;
}

void
sto_readdir_back_free(struct sto_readdir_back_req *req)
{
	sto_readdir_dirents_free(req);

	free((char *) req->dirname);
	free(req);
}

static int
sto_readdir_back_submit(struct sto_readdir_back_req *req)
{
	return sto_exec(&req->exec_ctx);
}

int
sto_readdir_back(const char *dirname, bool skip_hidden,
		 readdir_back_done_t readdir_back_done, void *priv)
{
	struct sto_readdir_back_req *req;
	int rc;

	req = sto_readdir_back_alloc(dirname, skip_hidden);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for back readdir\n");
		return -ENOMEM;
	}

	sto_readdir_back_init_cb(req, readdir_back_done, priv);

	rc = sto_readdir_back_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit back readdir, rc=%d\n", rc);
		goto free_readdir;
	}

	return 0;

free_readdir:
	sto_readdir_back_free(req);

	return rc;
}
