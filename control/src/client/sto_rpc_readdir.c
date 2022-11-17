#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "sto_lib.h"
#include "sto_client.h"
#include "sto_rpc_readdir.h"

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
	struct sto_dirents *dirents = *(struct sto_dirents **) out;

	return spdk_json_decode_array(val, sto_dirent_decode, dirents->dirents,
				      STO_DIRENT_MAX_CNT, &dirents->cnt, sizeof(struct sto_dirent));
}

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

void
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

struct sto_rpc_readdir_info {
	int returncode;
	struct sto_dirents *dirents;
};

static const struct spdk_json_object_decoder sto_rpc_readdir_info_decoders[] = {
	{"returncode", offsetof(struct sto_rpc_readdir_info, returncode), spdk_json_decode_int32},
	{"dirents", offsetof(struct sto_rpc_readdir_info, dirents), sto_dirents_decode},
};

struct sto_rpc_readdir_params {
	const char *dirpath;
	bool skip_hidden;
};

struct sto_rpc_readdir_cmd {
	struct sto_dirents *dirents;

	void *priv;
	sto_rpc_readdir_done_t done;
};

static struct sto_rpc_readdir_cmd *
sto_rpc_readdir_cmd_alloc(void)
{
	struct sto_rpc_readdir_cmd *cmd;

	cmd = rte_zmalloc(NULL, sizeof(*cmd), 0);
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Cann't allocate memory for STO readdir cmd\n");
		return NULL;
	}

	return cmd;
}

static void
sto_rpc_readdir_cmd_init_cb(struct sto_rpc_readdir_cmd *cmd, sto_rpc_readdir_done_t done, void *priv)
{
	cmd->done = done;
	cmd->priv = priv;
}

static void
sto_rpc_readdir_cmd_free(struct sto_rpc_readdir_cmd *cmd)
{
	rte_free(cmd);
}

static void
sto_rpc_readdir_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct sto_rpc_readdir_cmd *cmd = priv;
	struct sto_dirents *dirents = cmd->dirents;
	struct sto_rpc_readdir_info info = {
		.dirents = dirents
	};

	if (spdk_unlikely(rc)) {
		goto out;
	}

	if (spdk_json_decode_object(resp->result, sto_rpc_readdir_info_decoders,
				    SPDK_COUNTOF(sto_rpc_readdir_info_decoders), &info)) {
		SPDK_ERRLOG("Failed to decode readdir info\n");
		rc = -ENOMEM;
		goto out;
	}

	SPDK_NOTICELOG("GLEB: Get result from READDIR response: dir_cnt %d\n",
		       (int) dirents->cnt);

out:
	cmd->done(cmd->priv, rc);

	sto_rpc_readdir_cmd_free(cmd);
}

static void
sto_rpc_readdir_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_rpc_readdir_params *params = priv;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "dirpath", params->dirpath);
	spdk_json_write_named_bool(w, "skip_hidden", params->skip_hidden);

	spdk_json_write_object_end(w);
}

static int
sto_rpc_readdir_cmd_run(struct sto_rpc_readdir_cmd *cmd, struct sto_rpc_readdir_params *params)
{
	struct sto_client_args args = {
		.priv = cmd,
		.response_handler = sto_rpc_readdir_resp_handler,
	};

	return sto_client_send("readdir", params, sto_rpc_readdir_info_json, &args);
}

int
sto_rpc_readdir(const char *dirpath, struct sto_rpc_readdir_args *args)
{
	struct sto_rpc_readdir_cmd *cmd;
	struct sto_rpc_readdir_params params = {
		.dirpath = dirpath,
		.skip_hidden = true,
	};
	int rc;

	cmd = sto_rpc_readdir_cmd_alloc();
	if (spdk_unlikely(!cmd)) {
		SPDK_ERRLOG("Failed to alloc memory for readdir cmd\n");
		return -ENOMEM;
	}

	cmd->dirents = args->dirents;

	sto_rpc_readdir_cmd_init_cb(cmd, args->done, args->priv);

	rc = sto_rpc_readdir_cmd_run(cmd, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to submit readdir, rc=%d\n", rc);
		goto free_cmd;
	}

	return 0;

free_cmd:
	sto_rpc_readdir_cmd_free(cmd);

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
