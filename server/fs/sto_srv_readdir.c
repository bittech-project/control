#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/string.h>

#include "sto_srv_readdir.h"

static int sto_srv_readdir_exec(void *arg);
static void sto_srv_readdir_exec_done(void *arg, int rc);

static struct sto_exec_ops srv_readdir_ops = {
	.name = "readdir",
	.exec = sto_srv_readdir_exec,
	.exec_done = sto_srv_readdir_exec_done,
};

static struct sto_srv_dirent *
sto_srv_dirent_alloc(const char *name)
{
	struct sto_srv_dirent *dirent;

	dirent = calloc(1, sizeof(*dirent));
	if (spdk_unlikely(!dirent)) {
		printf("server: Failed to alloc dirent\n");
		return NULL;
	}

	dirent->name = strdup(name);
	if (spdk_unlikely(!dirent->name)) {
		printf("server: Failed to alloc dirent name\n");
		goto free_dirent;
	}

	return dirent;

free_dirent:
	free(dirent);

	return NULL;
}

static void
sto_srv_dirent_free(struct sto_srv_dirent *dirent)
{
	free(dirent->name);
	free(dirent);
}

static int
sto_srv_dirent_get_stat(struct sto_srv_dirent *dirent, const char *path)
{
	struct stat sb;
	char *full_path;
	int rc = 0;

	full_path = spdk_sprintf_alloc("%s/%s", path, dirent->name);
	if (spdk_unlikely(!full_path)) {
		return -ENOMEM;
	}

	if (lstat(full_path, &sb) == -1) {
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
sto_srv_dirent_info_json(struct sto_srv_dirent *dirent, struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "name", dirent->name);
	spdk_json_write_named_uint32(w, "mode", dirent->mode);

	spdk_json_write_object_end(w);
}

static void
sto_srv_dirents_init(struct sto_srv_dirents *dirents)
{
	TAILQ_INIT(&dirents->dirents);
}

static void
sto_srv_dirents_free(struct sto_srv_dirents *dirents)
{
	struct sto_srv_dirent *dirent, *tmp;

	TAILQ_FOREACH_SAFE(dirent, &dirents->dirents, list, tmp) {
		TAILQ_REMOVE(&dirents->dirents, dirent, list);

		sto_srv_dirent_free(dirent);
	}
}

static void
sto_srv_dirents_add(struct sto_srv_dirents *dirents, struct sto_srv_dirent *dirent)
{
	TAILQ_INSERT_TAIL(&dirents->dirents, dirent, list);
}

void
sto_srv_dirents_info_json(struct sto_srv_dirents *dirents,
			  struct spdk_json_write_ctx *w)
{
	struct sto_srv_dirent *dirent;

	spdk_json_write_named_array_begin(w, "dirents");

	TAILQ_FOREACH(dirent, &dirents->dirents, list) {
		sto_srv_dirent_info_json(dirent, w);
	}

	spdk_json_write_array_end(w);
}

struct sto_srv_readdir_params {
	char *dirpath;
	bool skip_hidden;
};

static void
sto_srv_readdir_params_free(struct sto_srv_readdir_params *params)
{
	free(params->dirpath);
}

static const struct spdk_json_object_decoder sto_srv_readdir_decoders[] = {
	{"dirpath", offsetof(struct sto_srv_readdir_params, dirpath), spdk_json_decode_string},
	{"skip_hidden", offsetof(struct sto_srv_readdir_params, skip_hidden), spdk_json_decode_bool},
};

struct sto_srv_readdir_req {
	struct sto_exec_ctx exec_ctx;

	struct sto_srv_readdir_params params;

	struct sto_srv_dirents dirents;

	void *priv;
	sto_srv_readdir_done_t done;
};

static struct sto_srv_readdir_req *
sto_srv_readdir_req_alloc(const struct spdk_json_val *params)
{
	struct sto_srv_readdir_req *req;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		printf("server: Cann't allocate memory for readdir req\n");
		return NULL;
	}

	if (spdk_json_decode_object(params, sto_srv_readdir_decoders,
				    SPDK_COUNTOF(sto_srv_readdir_decoders), &req->params)) {
		printf("server: Cann't decode readdir req params\n");
		goto free_req;
	}

	sto_exec_init_ctx(&req->exec_ctx, &srv_readdir_ops, req);

	sto_srv_dirents_init(&req->dirents);

	return req;

free_req:
	free(req);

	return NULL;
}

static void
sto_srv_readdir_req_init_cb(struct sto_srv_readdir_req *req,
			    sto_srv_readdir_done_t done, void *priv)
{
	req->done = done;
	req->priv = priv;
}

static void
sto_srv_readdir_req_free(struct sto_srv_readdir_req *req)
{
	sto_srv_dirents_free(&req->dirents);
	sto_srv_readdir_params_free(&req->params);
	free(req);
}

static int
sto_srv_readdir_req_submit(struct sto_srv_readdir_req *req)
{
	return sto_exec(&req->exec_ctx);
}

int
sto_srv_readdir(const struct spdk_json_val *params,
		struct sto_srv_readdir_args *args)
{
	struct sto_srv_readdir_req *req;
	int rc;

	req = sto_srv_readdir_req_alloc(params);
	if (spdk_unlikely(!req)) {
		printf("server: Failed to alloc memory for readdir req\n");
		return -ENOMEM;
	}

	sto_srv_readdir_req_init_cb(req, args->done, args->priv);

	rc = sto_srv_readdir_req_submit(req);
	if (spdk_unlikely(rc)) {
		printf("server: Failed to submit readdir, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_srv_readdir_req_free(req);

	return rc;
}

static void
sto_srv_readdir_exec_done(void *arg, int rc)
{
	struct sto_srv_readdir_req *req = arg;

	req->done(req->priv, &req->dirents, rc);

	sto_srv_readdir_req_free(req);
}

static int
sto_srv_readdir_exec(void *arg)
{
	struct sto_srv_readdir_req *req = arg;
	struct sto_srv_readdir_params *params = &req->params;
	struct dirent *entry;
	DIR *dir;
	int rc = 0;

	dir = opendir(params->dirpath);
	if (spdk_unlikely(!dir)) {
		printf("server: Failed to open %s dir\n", params->dirpath);
		return -errno;
	}

	entry = readdir(dir);

	while (entry != NULL) {
		struct sto_srv_dirent *dirent;

		if (params->skip_hidden && entry->d_name[0] == '.') {
			entry = readdir(dir);
			continue;
		}

		dirent = sto_srv_dirent_alloc(entry->d_name);
		if (spdk_unlikely(!dirent)) {
			sto_srv_dirents_free(&req->dirents);
			printf("server: Failed to alloc dirent\n");
			break;
		}

		rc = sto_srv_dirent_get_stat(dirent, params->dirpath);
		if (spdk_unlikely(rc)) {
			sto_srv_dirents_free(&req->dirents);
			printf("server: Failed to dirent to get stat\n");
			break;
		}

		sto_srv_dirents_add(&req->dirents, dirent);

		entry = readdir(dir);
	}

	rc = closedir(dir);
	if (spdk_unlikely(rc == -1)) {
		printf("server: Failed to close %s dir\n", params->dirpath);
		rc = -errno;
	}

	return rc;
}
