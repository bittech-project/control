#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_readdir_back.h"

static int sto_readdir_exec(void *arg);
static void sto_readdir_exec_done(void *arg, int rc);

static struct sto_exec_ops readdir_ops = {
	.name = "readdir",
	.exec = sto_readdir_exec,
	.exec_done = sto_readdir_exec_done,
};

static struct sto_dirent *
sto_readdir_alloc_dirent(const char *d_name)
{
	struct sto_dirent *dirent;

	dirent = calloc(1, sizeof(*dirent));
	if (spdk_unlikely(!dirent)) {
		printf("server: Failed to alloc dirent\n");
		return NULL;
	}

	dirent->d_name = strdup(d_name);
	if (spdk_unlikely(!dirent->d_name)) {
		printf("server: Failed to alloc dirent name\n");
		goto free_dirent;
	}

	return dirent;

free_dirent:
	free(dirent);

	return NULL;
}

static int
sto_dirent_get_stat(struct sto_dirent *dirent, const char *path)
{
	struct stat sb;
	char *full_path;
	int rc = 0;

	full_path = spdk_sprintf_alloc("%s/%s", path, dirent->d_name);
	if (spdk_unlikely(!full_path)) {
		return -ENOMEM;
	}

	if (stat(full_path, &sb) == -1) {
		printf("server: Failed to get stat for file %s: %s\n",
		       full_path, strerror(errno));
		rc = -errno;
		goto out;
	}

	dirent->mode = sb.st_mode;

out:
	free(full_path);

	return rc;
}

static void
sto_readdir_dirents_free(struct sto_readdir_back_req *req)
{
	struct sto_dirent *dirent, *tmp;

	TAILQ_FOREACH_SAFE(dirent, &req->dirent_list, list, tmp) {
		TAILQ_REMOVE(&req->dirent_list, dirent, list);

		free(dirent->d_name);
		free(dirent);
	}
}

static void
sto_readdir_exec_done(void *arg, int rc)
{
	struct sto_readdir_back_req *req = arg;

	req->returncode = rc;
	req->readdir_back_done(req);
}

static int
sto_readdir_exec(void *arg)
{
	struct sto_readdir_back_req *req = arg;
	struct dirent *entry;
	DIR *dir;
	int rc = 0;

	dir = opendir(req->dirpath);
	if (spdk_unlikely(!dir)) {
		printf("server: Failed to open %s dir\n", req->dirpath);
		return -errno;
	}

	entry = readdir(dir);

	while (entry != NULL) {
		struct sto_dirent *dirent;

		if (req->skip_hidden && entry->d_name[0] == '.') {
			entry = readdir(dir);
			continue;
		}

		dirent = sto_readdir_alloc_dirent(entry->d_name);
		if (spdk_unlikely(!dirent)) {
			sto_readdir_dirents_free(req);
			printf("server: Failed to alloc dirent\n");
			break;
		}

		rc = sto_dirent_get_stat(dirent, req->dirpath);
		if (spdk_unlikely(rc)) {
			sto_readdir_dirents_free(req);
			printf("server: Failed to dirent to get stat\n");
			break;
		}

		TAILQ_INSERT_TAIL(&req->dirent_list, dirent, list);

		entry = readdir(dir);
	}

	rc = closedir(dir);
	if (spdk_unlikely(rc == -1)) {
		printf("server: Failed to close %s dir\n", req->dirpath);
		rc = -errno;
	}

	return rc;
}

static struct sto_readdir_back_req *
sto_readdir_back_alloc(const char *dirpath, bool skip_hidden)
{
	struct sto_readdir_back_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("Cann't allocate memory for STO readdir req\n");
		return NULL;
	}

	req->dirpath = strdup(dirpath);
	if (spdk_unlikely(!req->dirpath)) {
		printf("Cann't allocate memory for dirpath: %s\n", dirpath);
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

	free((char *) req->dirpath);
	free(req);
}

static int
sto_readdir_back_submit(struct sto_readdir_back_req *req)
{
	return sto_exec(&req->exec_ctx);
}

int
sto_readdir_back(const char *dirpath, bool skip_hidden,
		 readdir_back_done_t readdir_back_done, void *priv)
{
	struct sto_readdir_back_req *req;
	int rc;

	req = sto_readdir_back_alloc(dirpath, skip_hidden);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for back readdir\n");
		return -ENOMEM;
	}

	sto_readdir_back_init_cb(req, readdir_back_done, priv);

	rc = sto_readdir_back_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit back readdir, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_readdir_back_free(req);

	return rc;
}
