#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "sto_client.h"
#include "sto_readdir.h"

static const struct spdk_json_object_decoder sto_dirent_decoders[] = {
	{"name", offsetof(struct sto_dirent, name), spdk_json_decode_string},
	{"mode", offsetof(struct sto_dirent, mode), spdk_json_decode_uint32},
};

static int
sto_dirent_decode(const struct spdk_json_val *val, void *out)
{
	struct sto_dirent *dirent = out;

	return spdk_json_decode_object(val, sto_dirent_decoders,
				       SPDK_COUNTOF(sto_dirent_decoders), dirent);
}

static int
sto_dirents_decode(const struct spdk_json_val *val, void *out)
{
	struct sto_dirents *dirents = out;

	return spdk_json_decode_array(val, sto_dirent_decode, dirents->dirents,
				      STO_DIRENT_MAX_CNT, &dirents->cnt, sizeof(struct sto_dirent));
}

static const struct spdk_json_object_decoder sto_readdir_result_decoders[] = {
	{"returncode", offsetof(struct sto_readdir_result, returncode), spdk_json_decode_int32},
	{"dirents", offsetof(struct sto_readdir_result, dirents), sto_dirents_decode},
};

int
sto_dirent_copy(struct sto_dirent *src, struct sto_dirent *dst)
{
	dst->name = strdup(src->name);
	if (spdk_unlikely(!dst->name)) {
		SPDK_ERRLOG("Failed to copy dirent name: %s\n", src->name);
		return -ENOMEM;
	}

	dst->mode = src->mode;

	return 0;
}

void
sto_dirent_free(struct sto_dirent *dirent)
{
	free(dirent->name);
}

static void
sto_dirents_free(struct sto_dirents *dirents)
{
	int i;

	if (spdk_unlikely(!dirents->cnt)) {
		return;
	}

	for (i = 0; i < dirents->cnt; i++) {
		sto_dirent_free(&dirents->dirents[i]);
	}

	memset(dirents, 0, sizeof(*dirents));
}

void
sto_readdir_result_free(struct sto_readdir_result *result)
{
	sto_dirents_free(&result->dirents);
}

static struct sto_readdir_req *
sto_readdir_alloc(const char *dirpath)
{
	struct sto_readdir_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for STO readdir req\n");
		return NULL;
	}

	req->dirpath = strdup(dirpath);
	if (spdk_unlikely(!req->dirpath)) {
		SPDK_ERRLOG("Cann't allocate memory for dirpath: %s\n", dirpath);
		goto free_req;
	}

	req->skip_hidden = true;

	return req;

free_req:
	rte_free(req);

	return NULL;
}

static void
sto_readdir_init_cb(struct sto_readdir_req *req, readdir_done_t readdir_done, void *priv)
{
	req->readdir_done = readdir_done;
	req->priv = priv;
}

static void
sto_readdir_free(struct sto_readdir_req *req)
{
	free((char *) req->dirpath);
	rte_free(req);
}

static void
sto_readdir_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct sto_readdir_req *req = priv;
	struct sto_readdir_result *result = req->result;

	if (spdk_unlikely(rc)) {
		result->returncode = rc;
		goto out;
	}

	if (spdk_json_decode_object(resp->result, sto_readdir_result_decoders,
				    SPDK_COUNTOF(sto_readdir_result_decoders), result)) {
		SPDK_ERRLOG("Failed to decode readdir result\n");
		result->returncode = -ENOMEM;
		goto out;
	}

	SPDK_NOTICELOG("GLEB: Get result from READDIR response: returncode[%d], dir_cnt %d\n",
		       result->returncode, (int) result->dirents.cnt);

out:
	req->readdir_done(req->priv);

	sto_readdir_free(req);
}

static void
sto_readdir_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_readdir_req *req = priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "dirpath", req->dirpath);
	spdk_json_write_named_bool(w, "skip_hidden", req->skip_hidden);

	spdk_json_write_object_end(w);
}

static int
sto_readdir_submit(struct sto_readdir_req *req)
{
	struct sto_client_args args = {
		.priv = req,
		.response_handler = sto_readdir_resp_handler,
	};

	return sto_client_send("readdir", req, sto_readdir_info_json, &args);
}

int
sto_readdir(const char *dirpath, struct sto_readdir_args *args)
{
	struct sto_readdir_req *req;
	int rc;

	req = sto_readdir_alloc(dirpath);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc memory for readdir req\n");
		return -ENOMEM;
	}

	req->result = args->result;
	sto_readdir_init_cb(req, args->readdir_done, args->priv);

	rc = sto_readdir_submit(req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to submit readdir, rc=%d\n", rc);
		goto free_req;
	}

	return 0;

free_req:
	sto_readdir_free(req);

	return rc;
}

static bool
find_match_str(const char **exclude_list, const char *str)
{
	const char **i;

	if (!exclude_list) {
		return false;
	}

	for (i = exclude_list; *i; i++) {
		if (!strcmp(str, *i)) {
			return true;
		}
	}

	return false;
}

void
sto_dirents_info_json(struct sto_dirents *dirents,
		      struct sto_dirents_json_cfg *cfg, struct spdk_json_write_ctx *w)
{
	int i;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, cfg->name);

	for (i = 0; i < dirents->cnt; i++) {
		struct sto_dirent *dirent = &dirents->dirents[i];

		if (find_match_str(cfg->exclude_list, dirent->name)) {
			continue;
		}

		if (cfg->type && ((dirent->mode & S_IFMT) != cfg->type)) {
			continue;
		}

		spdk_json_write_string(w, dirent->name);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}
