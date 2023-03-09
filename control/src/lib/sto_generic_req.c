#include "sto_generic_req.h"

#include <spdk/stdinc.h>
#include <spdk/json.h>

#include "sto_req.h"
#include "sto_rpc_aio.h"
#include "sto_pipeline.h"
#include "sto_rpc_readdir.h"
#include "sto_tree.h"

struct spdk_json_write_ctx;

static void
sto_write_req_params_deinit(void *params_ptr)
{
	struct sto_write_req_params *params = params_ptr;

	free((char *) params->file);
	free(params->data);
}

static void
sto_write_req_exec(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct sto_write_req_params *params = sto_req_get_params(req);

	sto_rpc_writefile(params->file, 0, params->data, sto_pipeline_step_done, pipe);
}

const struct sto_req_properties sto_write_req_properties = {
	.params_size = sizeof(struct sto_write_req_params),
	.params_deinit_fn = sto_write_req_params_deinit,

	.response = sto_dummy_req_response,
	.steps = {
		STO_PL_STEP(sto_write_req_exec, NULL),
		STO_PL_STEP_TERMINATOR(),
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

static void
sto_read_req_exec(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct sto_read_req_priv *priv = sto_req_get_priv(req);
	struct sto_read_req_params *params = sto_req_get_params(req);

	sto_rpc_readfile_buf(params->file, params->size,
			     sto_pipeline_step_done, pipe, &priv->buf);
}

static void
sto_read_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_read_req_priv *priv = sto_req_get_priv(req);

	spdk_json_write_string(w, priv->buf);
}

const struct sto_req_properties sto_read_req_properties = {
	.params_size = sizeof(struct sto_read_req_params),
	.params_deinit_fn = sto_read_req_params_deinit,

	.priv_size = sizeof(struct sto_read_req_priv),
	.priv_deinit_fn = sto_read_req_priv_deinit,

	.response = sto_read_req_response,
	.steps = {
		STO_PL_STEP(sto_read_req_exec, NULL),
		STO_PL_STEP_TERMINATOR(),
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

static void
sto_readlink_req_exec(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct sto_readlink_req_priv *priv = sto_req_get_priv(req);
	struct sto_readlink_req_params *params = sto_req_get_params(req);

	sto_rpc_readlink(params->file, sto_pipeline_step_done, pipe, &priv->buf);
}

static void
sto_readlink_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_readlink_req_priv *priv = sto_req_get_priv(req);

	spdk_json_write_string(w, priv->buf);
}

const struct sto_req_properties sto_readlink_req_properties = {
	.params_size = sizeof(struct sto_readlink_req_params),
	.params_deinit_fn = sto_readlink_req_params_deinit,

	.priv_size = sizeof(struct sto_readlink_req_priv),
	.priv_deinit_fn = sto_readlink_req_priv_deinit,

	.response = sto_readlink_req_response,
	.steps = {
		STO_PL_STEP(sto_readlink_req_exec, NULL),
		STO_PL_STEP_TERMINATOR(),
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

static void
sto_readdir_req_exec(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct sto_readdir_req_priv *priv = sto_req_get_priv(req);
	struct sto_readdir_req_params *params = sto_req_get_params(req);

	sto_rpc_readdir(params->dirpath, sto_pipeline_step_done, pipe, &priv->dirents);
}

static void
sto_readdir_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_readdir_req_priv *req_priv = sto_req_get_priv(req);
	struct sto_readdir_req_params *params = sto_req_get_params(req);
	struct sto_dirents_json_cfg cfg = {
		.name = params->name,
		.exclude_list = params->exclude_list,
	};

	sto_dirents_info_json(&req_priv->dirents, &cfg, w);
}

const struct sto_req_properties sto_readdir_req_properties = {
	.params_size = sizeof(struct sto_readdir_req_params),
	.params_deinit_fn = sto_readdir_req_params_deinit,

	.priv_size = sizeof(struct sto_readdir_req_priv),
	.priv_deinit_fn = sto_readdir_req_priv_deinit,

	.response = sto_readdir_req_response,
	.steps = {
		STO_PL_STEP(sto_readdir_req_exec, NULL),
		STO_PL_STEP_TERMINATOR(),
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

static void
sto_tree_req_exec(struct sto_pipeline *pipe)
{
	struct sto_req *req = sto_pipeline_get_priv(pipe);
	struct sto_tree_req_priv *priv = sto_req_get_priv(req);
	struct sto_tree_req_params *params = sto_req_get_params(req);

	sto_tree_buf(params->dirpath, params->depth, params->only_dirs,
		     sto_pipeline_step_done, pipe, &priv->tree_root);
}

static void
sto_tree_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	struct sto_tree_req_priv *priv = sto_req_get_priv(req);
	struct sto_tree_req_params *params = sto_req_get_params(req);
	struct sto_tree_node *tree_root = &priv->tree_root;

	if (params->info_json) {
		params->info_json(tree_root, w);
		return;
	}

	sto_tree_info_json(tree_root, w);
}

const struct sto_req_properties sto_tree_req_properties = {
	.params_size = sizeof(struct sto_tree_req_params),
	.params_deinit_fn = sto_tree_req_params_deinit,

	.priv_size = sizeof(struct sto_tree_req_priv),
	.priv_deinit_fn = sto_tree_req_priv_deinit,

	.response = sto_tree_req_response,
	.steps = {
		STO_PL_STEP(sto_tree_req_exec, NULL),
		STO_PL_STEP_TERMINATOR(),
	}
};
