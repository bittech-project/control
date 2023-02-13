#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_generic_req.h"
#include "sto_req.h"
#include "sto_rpc_aio.h"

static void
sto_write_req_params_deinit(void *params_ptr)
{
	struct sto_write_req_params *params = params_ptr;

	free((char *) params->file);
	free(params->data);
}

static int
sto_write_req_exec(struct sto_req *req)
{
	struct sto_write_req_params *params = req->type.params;
	struct sto_rpc_writefile_args args = {
		.priv = req,
		.done = sto_req_step_done,
	};

	return sto_rpc_writefile(params->file, 0, params->data, &args);
}

const struct sto_req_properties sto_write_req_properties = {
	.params_size = sizeof(struct sto_write_req_params),
	.params_deinit = sto_write_req_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_REQ_STEP(sto_write_req_exec, NULL),
		STO_REQ_STEP_TERMINATOR(),
	}
};

struct sto_read_req_priv {
	char *buf;
};

static void
sto_read_req_priv_deinit(void *priv_ptr)
{
	struct sto_read_req_priv *priv = priv_ptr;
	free(priv->buf);
}

static void
sto_read_req_params_deinit(void *params_ptr)
{
	struct sto_read_req_params *params = params_ptr;
	free((char *) params->file);
}

static int
sto_read_req_exec(struct sto_req *req)
{
	struct sto_read_req_priv *priv = req->type.priv;
	struct sto_read_req_params *params = req->type.params;
	struct sto_rpc_readfile_args args = {
		.priv = req,
		.done = sto_req_step_done,
		.buf = &priv->buf,
	};

	return sto_rpc_readfile(params->file, params->size, &args);
}

static void
sto_read_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_read_req_priv *priv = req->type.priv;

	spdk_json_write_string(w, priv->buf);
}

const struct sto_req_properties sto_read_req_properties = {
	.params_size = sizeof(struct sto_read_req_params),
	.params_deinit = sto_read_req_params_deinit,

	.priv_size = sizeof(struct sto_read_req_priv),
	.priv_deinit = sto_read_req_priv_deinit,

	.response = sto_read_req_response,
	.steps = {
		STO_REQ_STEP(sto_read_req_exec, NULL),
		STO_REQ_STEP_TERMINATOR(),
	}
};

static void
sto_readlink_req_params_deinit(void *params_ptr)
{
	struct sto_readlink_req_params *params = params_ptr;
	free((char *) params->file);
}

struct sto_readlink_req_priv {
	char *buf;
};

static void
sto_readlink_req_priv_deinit(void *priv_ptr)
{
	struct sto_readlink_req_priv *priv = priv_ptr;
	free(priv->buf);
}

static int
sto_readlink_req_exec(struct sto_req *req)
{
	struct sto_readlink_req_priv *priv = req->type.priv;
	struct sto_readlink_req_params *params = req->type.params;
	struct sto_rpc_readlink_args args = {
		.priv = req,
		.done = sto_req_step_done,
		.buf = &priv->buf,
	};

	return sto_rpc_readlink(params->file, &args);
}

static void
sto_readlink_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_readlink_req_priv *priv = req->type.priv;

	spdk_json_write_string(w, priv->buf);
}

const struct sto_req_properties sto_readlink_req_properties = {
	.params_size = sizeof(struct sto_readlink_req_params),
	.params_deinit = sto_readlink_req_params_deinit,

	.priv_size = sizeof(struct sto_readlink_req_priv),
	.priv_deinit = sto_readlink_req_priv_deinit,

	.response = sto_readlink_req_response,
	.steps = {
		STO_REQ_STEP(sto_readlink_req_exec, NULL),
		STO_REQ_STEP_TERMINATOR(),
	}
};

static void
sto_readdir_req_params_deinit(void *params_ptr)
{
	struct sto_readdir_req_params *params = params_ptr;
	free((char *) params->name);
	free(params->dirpath);
}

struct sto_readdir_req_priv {
	struct sto_dirents dirents;
};

static void
sto_readdir_req_priv_deinit(void *priv_ptr)
{
	struct sto_readdir_req_priv *priv = priv_ptr;
	sto_dirents_free(&priv->dirents);
}

static int
sto_readdir_req_exec(struct sto_req *req)
{
	struct sto_readdir_req_priv *priv = req->type.priv;
	struct sto_readdir_req_params *params = req->type.params;
	struct sto_rpc_readdir_args args = {
		.priv = req,
		.done = sto_req_step_done,
		.dirents = &priv->dirents,
	};

	return sto_rpc_readdir(params->dirpath, &args);
}

static void
sto_readdir_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_readdir_req_priv *priv = req->type.priv;
	struct sto_readdir_req_params *params = req->type.params;
	struct sto_dirents_json_cfg cfg = {
		.name = params->name,
		.exclude_list = params->exclude_list,
	};

	sto_dirents_info_json(&priv->dirents, &cfg, w);
}

const struct sto_req_properties sto_readdir_req_properties = {
	.params_size = sizeof(struct sto_readdir_req_params),
	.params_deinit = sto_readdir_req_params_deinit,

	.priv_size = sizeof(struct sto_readdir_req_priv),
	.priv_deinit = sto_readdir_req_priv_deinit,

	.response = sto_readdir_req_response,
	.steps = {
		STO_REQ_STEP(sto_readdir_req_exec, NULL),
		STO_REQ_STEP_TERMINATOR(),
	}
};

static void
sto_tree_req_params_deinit(void *params_ptr)
{
	struct sto_tree_req_params *params = params_ptr;
	free(params->dirpath);
}

struct sto_tree_req_priv {
	struct sto_tree_node tree_root;
};

static void
sto_tree_req_priv_deinit(void *priv_ptr)
{
	struct sto_tree_req_priv *priv = priv_ptr;
	sto_tree_free(&priv->tree_root);
}

static int
sto_tree_req_exec(struct sto_req *req)
{
	struct sto_tree_req_priv *priv = req->type.priv;
	struct sto_tree_req_params *params = req->type.params;
	struct sto_tree_args args = {
		.priv = req,
		.done = sto_req_step_done,
		.tree_root = &priv->tree_root,
	};

	return sto_tree(params->dirpath, params->depth, params->only_dirs, &args);
}

static void
sto_tree_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_tree_req_priv *priv = req->type.priv;
	struct sto_tree_req_params *params = req->type.params;
	struct sto_tree_node *tree_root = &priv->tree_root;

	if (params->info_json) {
		params->info_json(tree_root, w);
		return;
	}

	sto_tree_info_json(tree_root, w);
}

const struct sto_req_properties sto_tree_req_properties = {
	.params_size = sizeof(struct sto_tree_req_params),
	.params_deinit = sto_tree_req_params_deinit,

	.priv_size = sizeof(struct sto_tree_req_priv),
	.priv_deinit = sto_tree_req_priv_deinit,

	.response = sto_tree_req_response,
	.steps = {
		STO_REQ_STEP(sto_tree_req_exec, NULL),
		STO_REQ_STEP_TERMINATOR(),
	}
};
