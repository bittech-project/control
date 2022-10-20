#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_client.h"
#include "sto_readdir_front.h"

static struct sto_readdir_req *
sto_readdir_alloc(const char *dirname)
{
	struct sto_readdir_req *req;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Cann't allocate memory for STO readdir req\n");
		return NULL;
	}

	req->dirname = strdup(dirname);
	if (spdk_unlikely(!req->dirname)) {
		SPDK_ERRLOG("Cann't allocate memory for dirname: %s\n", dirname);
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

void
sto_readdir_free(struct sto_readdir_req *req)
{
	sto_dirents_free(&req->dirents);

	free((char *) req->dirname);
	rte_free(req);
}

#define STO_DIRENT_MAX_CNT 256

struct sto_dirent_list {
	const char *entries[STO_DIRENT_MAX_CNT];
	size_t cnt;
};

static int
sto_dirent_list_decode(const struct spdk_json_val *val, void *out)
{
	struct sto_dirent_list *dirent_list = out;

	return spdk_json_decode_array(val, spdk_json_decode_string, dirent_list->entries, STO_DIRENT_MAX_CNT,
				      &dirent_list->cnt, sizeof(char *));
}

static void
sto_dirent_list_free(struct sto_dirent_list *dirent_list)
{
	ssize_t i;

	for (i = 0; i < dirent_list->cnt; i++) {
		free((char *) dirent_list->entries[i]);
	}
}

struct sto_readdir_result {
	int returncode;
	struct sto_dirent_list dirent_list;
};

static void
sto_readdir_result_free(struct sto_readdir_result *result)
{
	sto_dirent_list_free(&result->dirent_list);
}

static const struct spdk_json_object_decoder sto_readdir_result_decoders[] = {
	{"returncode", offsetof(struct sto_readdir_result, returncode), spdk_json_decode_int32},
	{"dirents", offsetof(struct sto_readdir_result, dirent_list), sto_dirent_list_decode},
};

static void
sto_readdir_resp_handler(struct sto_rpc_request *rpc_req,
			 struct spdk_jsonrpc_client_response *resp)
{
	struct sto_readdir_req *req = rpc_req->priv;
	struct sto_readdir_result result;
	struct sto_dirent_list *dirent_list;
	int rc;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_readdir_result_decoders,
				    SPDK_COUNTOF(sto_readdir_result_decoders), &result)) {
		SPDK_ERRLOG("Failed to decode response for subprocess\n");
		goto out;
	}

	dirent_list = &result.dirent_list;

	SPDK_NOTICELOG("GLEB: Get result from READDIR response: returncode[%d], dir_cnt %d\n",
		       result.returncode, (int) result.dirent_list.cnt);

	req->returncode = result.returncode;

	if (spdk_unlikely(req->returncode)) {
		goto free_result;
	}

	rc = sto_dirents_init(&req->dirents, dirent_list->entries, dirent_list->cnt);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to copy dirents, rc=%d\n", rc);
		req->returncode = rc;
		goto free_result;
	}

free_result:
	sto_readdir_result_free(&result);

out:
	sto_rpc_req_free(rpc_req);

	req->readdir_done(req);
}

static void
sto_readdir_info_json(struct sto_rpc_request *rpc_req, struct spdk_json_write_ctx *w)
{
	struct sto_readdir_req *req = rpc_req->priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "dirname", req->dirname);
	spdk_json_write_named_bool(w, "skip_hidden", req->skip_hidden);

	spdk_json_write_object_end(w);
}

static int
sto_readdir_submit(struct sto_readdir_req *req)
{
	struct sto_rpc_request *rpc_req;
	int rc = 0;

	rpc_req = sto_rpc_req_alloc("readdir", sto_readdir_info_json, req);
	if (spdk_unlikely(!rpc_req)) {
		SPDK_ERRLOG("Failed to alloc readdir RPC req\n");
		return -ENOMEM;
	}

	sto_rpc_req_init_cb(rpc_req, sto_readdir_resp_handler);

	rc = sto_client_send(rpc_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to send RPC req, rc=%d\n", rc);
		sto_rpc_req_free(rpc_req);
	}

	return rc;
}

int
sto_readdir(const char *dirname, readdir_done_t readdir_done, void *priv)
{
	struct sto_readdir_req *req;
	int rc;

	req = sto_readdir_alloc(dirname);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc memory for readdir req\n");
		return -ENOMEM;
	}

	sto_readdir_init_cb(req, readdir_done, priv);

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

int
sto_dirents_init(struct sto_dirents *dirents, const char **entries, int cnt)
{
	int i, j;

	dirents->entries = calloc(cnt, sizeof(char *));
	if (spdk_unlikely(!dirents->entries)) {
		SPDK_ERRLOG("Failed to alloc %d dirents\n", cnt);
		return -ENOMEM;
	}

	for (i = 0; i < cnt; i++) {
		dirents->entries[i] = strdup(entries[i]);
		if (spdk_unlikely(!dirents->entries[i])) {
			SPDK_ERRLOG("Failed to copy %d dirent\n", i);
			goto free_dirents;
		}
	}

	dirents->cnt = cnt;

	return 0;

free_dirents:
	for (j = 0; j < i; j++) {
		free((char *) dirents->entries[j]);
	}

	free(dirents->entries);

	return -ENOMEM;
}

void
sto_dirents_free(struct sto_dirents *dirents)
{
	int i;

	if (spdk_unlikely(!dirents->cnt)) {
		return;
	}

	for (i = 0; i < dirents->cnt; i++) {
		free((char *) dirents->entries[i]);
	}

	free(dirents->entries);
}

void
sto_dirents_dump_json(struct sto_dirents *dirents, const char *dirname,
		      struct spdk_json_write_ctx *w)
{
	int i;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, dirname);

	for (i = 0; i < dirents->cnt; i++) {
		spdk_json_write_string(w, dirents->entries[i]);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}