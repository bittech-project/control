#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/queue.h>
#include <spdk/likely.h>
#include <spdk/log.h>
#include <spdk/string.h>

#include "scst.h"

#include "sto_json.h"
#include "sto_pipeline.h"
#include "sto_err.h"
#include "sto_rpc_aio.h"
#include "sto_tree.h"

#define SCST_DEF_CONFIG_PATH "/etc/control.scst.json"

struct scst_json_ctx {
	struct sto_json_ctx json;
};

static inline void
scst_json_ctx_deinit(void *ctx_ptr)
{
	struct scst_json_ctx *ctx = ctx_ptr;

	sto_json_ctx_destroy(&ctx->json);
}

struct scst_device_handler;

struct scst_device {
	struct scst_device_handler *handler;

	const char *name;
	const char *path;

	TAILQ_ENTRY(scst_device) list;
};

static void scst_device_free(struct scst_device *device);
static void scst_device_destroy(struct scst_device *device);

struct scst_device_handler {
	const char *name;
	const char *path;
	const char *mgmt_path;

	TAILQ_HEAD(, scst_device) device_list;
	int ref_cnt;

	TAILQ_ENTRY(scst_device_handler) list;
};

static void scst_device_handler_free(struct scst_device_handler *handler);

struct scst {
	const char *sys_path;
	const char *config_path;

	struct sto_pipeline_engine *engine;

	TAILQ_HEAD(, scst_device_handler) handler_list;
};

static struct scst *g_scst;

static struct scst_device_handler *scst_get_device_handler(const char *handler_name);
static void scst_put_device_handler(struct scst_device_handler *handler);
static int scst_add_device(const char *handler_name, const char *device_name);
static int scst_remove_device(const char *handler_name, const char *device_name);
static struct scst_device *scst_find_device(const char *handler_name, const char *device_name);
static void scst_write_config(sto_generic_cb cb_fn, void *cb_arg);

static inline void
scst_write_config_step(struct sto_pipeline *pipe)
{
	scst_write_config(sto_pipeline_step_done, pipe);
}


static struct scst_device_handler *
scst_device_handler_alloc(const char *handler_name)
{
	struct scst_device_handler *handler;

	handler = calloc(1, sizeof(*handler));
	if (spdk_unlikely(!handler)) {
		SPDK_ERRLOG("Failed to alloc SCST dev handler\n");
		return NULL;
	}

	handler->name = strdup(handler_name);
	if (spdk_unlikely(!handler->name)) {
		SPDK_ERRLOG("Failed to alloc dev handler name %s\n", handler_name);
		goto out_err;
	}

	handler->path = spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_HANDLERS, handler->name);
	if (spdk_unlikely(!handler->path)) {
		SPDK_ERRLOG("Failed to alloc dev handler path\n");
		goto out_err;
	}

	handler->mgmt_path = spdk_sprintf_alloc("%s/%s", handler->path, SCST_MGMT_IO);
	if (spdk_unlikely(!handler->mgmt_path)) {
		SPDK_ERRLOG("Failed to alloc dev handler mgmt path\n");
		goto out_err;
	}

	TAILQ_INIT(&handler->device_list);

	return handler;

out_err:
	scst_device_handler_free(handler);

	return NULL;
}

static void
scst_device_handler_free(struct scst_device_handler *handler)
{
	free((char *) handler->mgmt_path);
	free((char *) handler->path);
	free((char *) handler->name);
	free(handler);
}

static void
scst_device_handler_destroy(struct scst_device_handler *handler)
{
	struct scst_device *device, *tmp;

	TAILQ_FOREACH_SAFE(device, &handler->device_list, list, tmp) {
		scst_device_destroy(device);
	}
}

static struct scst_device *
scst_device_handler_find(struct scst_device_handler *handler, const char *device_name)
{
	struct scst_device *device;

	TAILQ_FOREACH(device, &handler->device_list, list) {
		if (!strcmp(device_name, device->name)) {
			return device;
		}
	}

	return NULL;
}

static inline struct scst_device_handler *
scst_device_handler_next(struct scst_device_handler *handler)
{
	struct scst *scst = g_scst;
	return !handler ? TAILQ_FIRST(&scst->handler_list) : TAILQ_NEXT(handler, list);
}

static struct scst_device *
scst_device_alloc(struct scst_device_handler *handler, const char *name)
{
	struct scst_device *device;

	device = calloc(1, sizeof(*device));
	if (spdk_unlikely(!device)) {
		SPDK_ERRLOG("Failed to alloc SCST device\n");
		return NULL;
	}

	device->name = strdup(name);
	if (spdk_unlikely(!device->name)) {
		SPDK_ERRLOG("Failed to alloc SCST device name\n");
		goto out_err;
	}

	device->path = spdk_sprintf_alloc("%s/%s", handler->path, device->name);
	if (spdk_unlikely(!device->path)) {
		SPDK_ERRLOG("Failed to alloc SCST device path\n");
		goto out_err;
	}

	device->handler = handler;

	return device;

out_err:
	scst_device_free(device);

	return NULL;
}

static void
scst_device_free(struct scst_device *device)
{
	free((char *) device->path);
	free((char *) device->name);
	free(device);
}

static void
scst_device_destroy(struct scst_device *device)
{
	struct scst_device_handler *handler = device->handler;

	TAILQ_REMOVE(&handler->device_list, device, list);

	scst_put_device_handler(handler);

	scst_device_free(device);
}

static inline struct scst_device *
scst_device_next(struct scst_device_handler *handler, struct scst_device *device)
{
	return !device ? TAILQ_FIRST(&handler->device_list) : TAILQ_NEXT(device, list);
}

static struct sto_rpc_writefile_args *
device_open_create_args(const char *handler_name, const char *device_name, const char *attributes)
{
	struct sto_rpc_writefile_args *args;

	args = calloc(1, sizeof(*args));
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to alloc writefile args\n");
		return NULL;
	}

	args->filepath = scst_handler_mgmt(handler_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("add_device %s", device_name);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	if (attributes) {
		char *tmp;

		tmp = spdk_sprintf_append_realloc(args->buf, " %s", attributes);
		if (spdk_unlikely(!tmp)) {
			SPDK_ERRLOG("Failed to realloc writefile args buf\n");
			goto out_err;
		}

		args->buf = tmp;
	}

	return args;

out_err:
	sto_rpc_writefile_args_free(args);

	return NULL;
}

static void
device_open(const char *handler_name, const char *device_name, const char *attributes,
	    sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args *args;

	args = device_open_create_args(handler_name, device_name, attributes);
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to create writefile args for `device_open`\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	SPDK_ERRLOG("SCST device open, filepath[%s], buf[%s]\n", args->filepath, args->buf);

	sto_rpc_writefile_args(args, 0, cb_fn, cb_arg);
}

static struct sto_rpc_writefile_args *
device_close_create_args(const char *handler_name, const char *device_name)
{
	struct sto_rpc_writefile_args *args;

	args = calloc(1, sizeof(*args));
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to alloc writefile args\n");
		return NULL;
	}

	args->filepath = scst_handler_mgmt(handler_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("del_device %s", device_name);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	return args;

out_err:
	sto_rpc_writefile_args_free(args);

	return NULL;
}

static void
device_close(const char *handler_name, const char *device_name,
	     sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args *args;

	args = device_close_create_args(handler_name, device_name);
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to create writefile args for `device_close`\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	SPDK_ERRLOG("SCST device close, filepath[%s], data[%s]\n", args->filepath, args->buf);

	sto_rpc_writefile_args(args, 0, cb_fn, cb_arg);
}

static void
device_open_step(struct sto_pipeline *pipe)
{
	struct scst_device_open_params *params = sto_pipeline_get_priv(pipe);

	if (scst_find_device(params->handler_name, params->device_name)) {
		sto_pipeline_step_next(pipe, -EEXIST);
		return;
	}

	device_open(params->handler_name, params->device_name, params->attributes,
		    sto_pipeline_step_done, pipe);
}

static void
device_open_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_device_open_params *params = sto_pipeline_get_priv(pipe);

	device_close(params->handler_name, params->device_name, sto_pipeline_step_done, pipe);
}

static void
device_open_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_device_open_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_add_device(params->handler_name, params->device_name);

	sto_pipeline_step_next(pipe, rc);
}

static void
device_open_cfg_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_device_open_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_remove_device(params->handler_name, params->device_name);
	assert(!rc);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_device_open_properties = {
	.steps = {
		STO_PL_STEP(device_open_step, device_open_rollback_step),
		STO_PL_STEP(device_open_cfg_step, device_open_cfg_rollback_step),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_device_open(struct scst_device_open_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(&scst_device_open_properties, cb_fn, cb_arg, params);
}

static void
device_close_step(struct sto_pipeline *pipe)
{
	struct scst_device_close_params *params = sto_pipeline_get_priv(pipe);

	if (!scst_find_device(params->handler_name, params->device_name)) {
		sto_pipeline_step_next(pipe, -ENOENT);
		return;
	}

	device_close(params->handler_name, params->device_name, sto_pipeline_step_done, pipe);
}

static void
device_close_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_device_open_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_remove_device(params->handler_name, params->device_name);
	assert(!rc);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_device_close_properties = {
	.steps = {
		STO_PL_STEP(device_close_step, NULL),
		STO_PL_STEP(device_close_cfg_step, NULL),
		STO_PL_STEP(scst_write_config_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_device_close(struct scst_device_close_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(&scst_device_close_properties, cb_fn, cb_arg, params);
}

static struct scst *
scst_create(void)
{
	struct scst *scst;

	scst = calloc(1, sizeof(*scst));
	if (spdk_unlikely(!scst)) {
		SPDK_ERRLOG("Failed to alloc SCST instance\n");
		return NULL;
	}

	scst->sys_path = SCST_ROOT;

	scst->config_path = strdup(SCST_DEF_CONFIG_PATH);
	if (spdk_unlikely(!scst->config_path)) {
		SPDK_ERRLOG("Failed to strdup config path for SCST\n");
		goto free_scst;
	}

	scst->engine = sto_pipeline_engine_create("SCST subsystem");
	if (spdk_unlikely(!scst->engine)) {
		SPDK_ERRLOG("Cann't create the SCST engine\n");
		goto free_config_path;
	}

	TAILQ_INIT(&scst->handler_list);

	return scst;

free_config_path:
	free((char *) scst->config_path);

free_scst:
	free(scst);

	return NULL;
}

static void
scst_destroy(struct scst *scst)
{
	sto_pipeline_engine_destroy(scst->engine);
	free((char *) scst->config_path);
	free(scst);
}

static struct scst_device_handler *
scst_find_device_handler(struct scst *scst, const char *handler_name)
{
	struct scst_device_handler *handler;

	TAILQ_FOREACH(handler, &scst->handler_list, list) {
		if (!strcmp(handler_name, handler->name)) {
			return handler;
		}
	}

	return NULL;
}

static struct scst_device_handler *
scst_get_device_handler(const char *handler_name)
{
	struct scst *scst = g_scst;
	struct scst_device_handler *handler;

	handler = scst_find_device_handler(scst, handler_name);
	if (!handler) {
		handler = scst_device_handler_alloc(handler_name);
		if (spdk_unlikely(!handler)) {
			SPDK_ERRLOG("Failed to alloc %s handler\n",
				    handler_name);
			return NULL;
		}

		TAILQ_INSERT_TAIL(&scst->handler_list, handler, list);
	}

	handler->ref_cnt++;

	return handler;
}

static void
scst_put_device_handler(struct scst_device_handler *handler)
{
	struct scst *scst = g_scst;
	int ref_cnt = --handler->ref_cnt;

	assert(ref_cnt >= 0);

	if (ref_cnt == 0) {
		TAILQ_REMOVE(&scst->handler_list, handler, list);
		scst_device_handler_free(handler);
	}
}

static struct scst_device *
scst_find_device(const char *handler_name, const char *device_name)
{
	struct scst *scst = g_scst;
	struct scst_device_handler *handler;

	handler = scst_find_device_handler(scst, handler_name);
	if (spdk_unlikely(!handler)) {
		return NULL;
	}

	return scst_device_handler_find(handler, device_name);
}

static int
scst_add_device(const char *handler_name, const char *device_name)
{
	struct scst_device_handler *handler;
	struct scst_device *device;

	if (scst_find_device(handler_name, device_name)) {
		SPDK_ERRLOG("SCST device %s is already exist\n", device_name);
		return -EEXIST;
	}

	handler = scst_get_device_handler(handler_name);
	if (spdk_unlikely(!handler)) {
		SPDK_ERRLOG("Failed to get %s handler\n", handler_name);
		return -ENOMEM;
	}

	device = scst_device_alloc(handler, device_name);
	if (spdk_unlikely(!device)) {
		SPDK_ERRLOG("Failed to alloc %s SCST device\n", device_name);
		goto put_handler;
	}

	TAILQ_INSERT_TAIL(&handler->device_list, device, list);

	SPDK_ERRLOG("SCST device %s handler [%s] was added\n",
		    device->name, handler->name);

	return 0;

put_handler:
	scst_put_device_handler(handler);

	return -ENOMEM;
}

static int
scst_remove_device(const char *handler_name, const char *device_name)
{
	struct scst_device *device;

	device = scst_find_device(handler_name, device_name);
	if (spdk_unlikely(!device)) {
		SPDK_ERRLOG("Failed to find `%s` SCST device to remove\n",
			    device_name);
		return -ENOENT;
	}

	scst_device_destroy(device);

	return 0;
}

static void
device_dumps_json(struct sto_tree_node *device_lnk_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *device_node;

	device_node = sto_tree_node_resolv_lnk(device_lnk_node);
	if (spdk_unlikely(!device_node)) {
		SPDK_ERRLOG("Failed to resolve device %s link\n",
			    device_lnk_node->inode->name);
		return;
	}

	spdk_json_write_name(w, device_lnk_node->inode->name);

	spdk_json_write_object_begin(w);

	scst_serialize_attrs(device_node, w);

	spdk_json_write_object_end(w);
}

static void
handler_dumps_json(struct sto_tree_node *handler, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *device_node, *mgmt_node;

	mgmt_node = sto_tree_node_find(handler, "mgmt");
	if (spdk_unlikely(!mgmt_node)) {
		SPDK_ERRLOG("Failed to find 'mgmt' for handler %s\n",
			    handler->inode->name);
		return;
	}

	spdk_json_write_name(w, handler->inode->name);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "devices");

	STO_TREE_FOREACH_TYPE(device_node, handler, STO_INODE_TYPE_LNK) {
		spdk_json_write_object_begin(w);
		device_dumps_json(device_node, w);
		spdk_json_write_object_end(w);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}

static bool
handler_list_is_empty(struct sto_tree_node *handler_list_node)
{
	struct sto_tree_node *handler_node;

	if (sto_tree_node_first_child_type(handler_list_node, STO_INODE_TYPE_DIR) == NULL) {
		return true;
	}

	STO_TREE_FOREACH_TYPE(handler_node, handler_list_node, STO_INODE_TYPE_DIR) {
		if (sto_tree_node_first_child_type(handler_node, STO_INODE_TYPE_LNK) != NULL) {
			return false;
		}
	}

	return true;
}

static void
handler_list_dumps_json(struct sto_tree_node *tree_root, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *handler_list_node, *handler_node;

	handler_list_node = sto_tree_node_find(tree_root, "handlers");

	if (!handler_list_node || handler_list_is_empty(handler_list_node)) {
		return;
	}

	spdk_json_write_named_array_begin(w, "handlers");

	STO_TREE_FOREACH_TYPE(handler_node, handler_list_node, STO_INODE_TYPE_DIR) {
		if (sto_tree_node_first_child_type(handler_node, STO_INODE_TYPE_LNK) == NULL) {
			continue;
		}

		spdk_json_write_object_begin(w);
		handler_dumps_json(handler_node, w);
		spdk_json_write_object_end(w);
	}

	spdk_json_write_array_end(w);
}

static int
dumps_json_write_cb(void *cb_ctx, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *tree_root = cb_ctx;

	spdk_json_write_object_begin(w);

	handler_list_dumps_json(tree_root, w);

	spdk_json_write_object_end(w);

	return 0;
}

struct dumps_json_ctx {
	struct sto_json_ctx *json;

	sto_generic_cb cb_fn;
	void *cb_arg;
};

static void
dumps_json(void *cb_arg, struct sto_tree_node *tree_root, int rc)
{
	struct dumps_json_ctx *ctx = cb_arg;

	if (spdk_unlikely(rc)) {
		rc = -ENOENT;
		goto out;
	}

	rc = sto_json_ctx_write(ctx->json, true, dumps_json_write_cb, (void *) tree_root);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to dump SCST dev attributes\n");
		goto out;
	}

	sto_json_print("Print dumped SCST configuration", ctx->json->values);

out:
	ctx->cb_fn(ctx->cb_arg, rc);
	free(ctx);

	sto_tree_free(tree_root);
}

void
scst_dumps_json(sto_generic_cb cb_fn, void *cb_arg, struct sto_json_ctx *json)
{
	struct scst *scst = g_scst;
	struct dumps_json_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc context to dumps JSON\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	ctx->json = json;

	ctx->cb_fn = cb_fn;
	ctx->cb_arg = cb_arg;

	sto_tree(scst->sys_path, 0, false, dumps_json, ctx);
}

static struct spdk_json_val *
device_json_iter_next(struct spdk_json_val *json, struct spdk_json_val *object)
{
	if (!object) {
		struct spdk_json_val *device_arr = sto_json_value(spdk_json_object_first(json));
		int rc = 0;

		rc = spdk_json_find_array(device_arr, "devices", NULL, &device_arr);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Could not find devices array from JSON\n");
			return NULL;
		}

		return spdk_json_array_first(device_arr);
	}

	return spdk_json_next(object);
}

static struct spdk_json_val *
handler_json_iter_next(struct spdk_json_val *json, struct spdk_json_val *object)
{
	if (!object) {
		struct spdk_json_val *handler_arr = json;
		int rc;

		rc = spdk_json_find_array(handler_arr, "handlers", NULL, &handler_arr);
		if (rc) {
			SPDK_ERRLOG("Could not find handlers array from JSON\n");
			return NULL;
		}

		return spdk_json_array_first(handler_arr);
	}

	return spdk_json_next(object);
}

static void
device_load_json(struct sto_json_async_iter *iter)
{
	struct spdk_json_val *handler = sto_json_async_iter_get_json(iter);
	struct spdk_json_val *device = sto_json_async_iter_get_object(iter);
	char *handler_name = NULL, *device_name = NULL;
	int rc = 0;

	if (spdk_json_decode_string(spdk_json_object_first(handler), &handler_name)) {
		SPDK_ERRLOG("Failed to decode handler name\n");
		rc = -EINVAL;
		goto out;
	}

	if (spdk_json_decode_string(spdk_json_object_first(device), &device_name)) {
		SPDK_ERRLOG("Failed to decode device name\n");
		rc = -EINVAL;
		goto free_handler_name;
	}

	rc = scst_add_device(handler_name, device_name);

	free(device_name);

free_handler_name:
	free(handler_name);

out:
	sto_json_async_iter_next(iter, rc);
}

static void
handler_load_json(struct sto_json_async_iter *iter)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_json_async_iter_get_object(iter),
		.iterate_fn = device_load_json,
		.next_fn = device_json_iter_next,
	};

	sto_json_async_iter_start(&opts, sto_json_async_iterate_done, iter);
}

static void
handler_list_load_json_step(struct sto_pipeline *pipe)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_pipeline_get_priv(pipe),
		.iterate_fn = handler_load_json,
		.next_fn = handler_json_iter_next,
	};

	sto_json_async_iter_start(&opts, sto_pipeline_step_done, pipe);
}

static const struct sto_pipeline_properties scst_load_json_properties = {
	.steps = {
		STO_PL_STEP(handler_list_load_json_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

static void
scst_load_json(struct spdk_json_val *json, sto_generic_cb cb_fn, void *cb_arg)
{
	sto_json_print("SCST load json", json);
	scst_pipeline(&scst_load_json_properties, cb_fn, cb_arg, json);
}

static void
scan_system_dumps_json_step(struct sto_pipeline *pipe)
{
	struct scst_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	scst_dumps_json(sto_pipeline_step_done, pipe, &ctx->json);
}

static void
scan_system_parse_json_step(struct sto_pipeline *pipe)
{
	struct scst_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	scst_load_json((struct spdk_json_val *) ctx->json.values, sto_pipeline_step_done, pipe);
}

static const struct sto_pipeline_properties scst_scan_system_properties = {
	.ctx_size = sizeof(struct scst_json_ctx),
	.ctx_deinit_fn = scst_json_ctx_deinit,

	.steps = {
		STO_PL_STEP(scan_system_dumps_json_step, NULL),
		STO_PL_STEP(scan_system_parse_json_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_scan_system(sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(&scst_scan_system_properties, cb_fn, cb_arg, NULL);
}

struct info_json_ctx {
	struct scst_device_handler *handler;
	struct scst_device *device;
};

static void
info_json_start_step(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);

	spdk_json_write_object_begin(w);

	sto_pipeline_step_next(pipe, 0);
}

static void
device_read_attrs_done(void *cb_arg, struct sto_json_ctx *json, int rc)
{
	struct sto_pipeline *pipe = cb_arg;
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	struct spdk_json_val *it;

	if (spdk_unlikely(rc)) {
		goto out;
	}

	spdk_json_write_name(w, ctx->device->name);

	spdk_json_write_object_begin(w);
	for (it = spdk_json_object_first((struct spdk_json_val *) json->values);
	     it != NULL;
	     it = spdk_json_next(it)) {

		spdk_json_write_val(w, it);
		if (it->type == SPDK_JSON_VAL_NAME) {
			spdk_json_write_val(w, sto_json_value(it));
		}
	}
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

out:
	sto_pipeline_step_next(pipe, rc);
}

static void
device_json_constructor(struct sto_pipeline *pipe)
{
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	scst_read_attrs(ctx->device->path, device_read_attrs_done, pipe);
}

static int
device_list_json_constructor(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	ctx->device = scst_device_next(ctx->handler, ctx->device);
	if (ctx->device) {
		spdk_json_write_object_begin(w);

		sto_pipeline_queue_step(pipe, STO_PL_STEP(device_json_constructor, NULL));
		return 0;
	}

	spdk_json_write_array_end(w);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	return STO_PL_CONSTRUCTOR_FINISHED;
}

static int
handler_list_json_constructor(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	ctx->handler = scst_device_handler_next(ctx->handler);
	if (ctx->handler) {
		spdk_json_write_object_begin(w);

		spdk_json_write_name(w, ctx->handler->name);
		spdk_json_write_object_begin(w);
		spdk_json_write_named_array_begin(w, "devices");

		sto_pipeline_queue_step(pipe, STO_PL_STEP_CONSTRUCTOR(device_list_json_constructor, NULL));
		return 0;
	}

	spdk_json_write_array_end(w);

	return STO_PL_CONSTRUCTOR_FINISHED;
}

static void
info_json_handler_list_step(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct scst *scst = g_scst;
	int rc = 0;

	if (TAILQ_EMPTY(&scst->handler_list)) {
		goto out;
	}

	spdk_json_write_named_array_begin(w, "handlers");

	rc = sto_pipeline_insert_step(pipe, STO_PL_STEP_CONSTRUCTOR(handler_list_json_constructor, NULL));

out:
	sto_pipeline_step_next(pipe, rc);
}

static void
info_json_end_step(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);

	spdk_json_write_object_end(w);

	sto_pipeline_step_next(pipe, 0);
}

static const struct sto_pipeline_properties scst_info_json_properties = {
	.ctx_size = sizeof(struct info_json_ctx),

	.steps = {
		STO_PL_STEP(info_json_start_step, NULL),
		STO_PL_STEP(info_json_handler_list_step, NULL),
		STO_PL_STEP(info_json_end_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

static void
scst_info_json(void *cb_ctx, struct spdk_json_write_ctx *w,
	       sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(&scst_info_json_properties, cb_fn, cb_arg, w);
}

static void
write_config_dumps_step(struct sto_pipeline *pipe)
{
	struct scst_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	sto_json_ctx_async_write(&ctx->json, true, scst_info_json, NULL,
				 sto_pipeline_step_done, pipe);
}

static void
write_config_save_step(struct sto_pipeline *pipe)
{
	struct scst_json_ctx *ctx = sto_pipeline_get_ctx(pipe);
	struct scst *scst = g_scst;

	sto_rpc_writefile(scst->config_path, O_CREAT | O_TRUNC | O_SYNC,
			  ctx->json.buf, sto_pipeline_step_done, pipe);
}

static const struct sto_pipeline_properties scst_write_config_properties = {
	.ctx_size = sizeof(struct scst_json_ctx),
	.ctx_deinit_fn = scst_json_ctx_deinit,

	.steps = {
		STO_PL_STEP(write_config_dumps_step, NULL),
		STO_PL_STEP(write_config_save_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

static void
scst_write_config(sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(&scst_write_config_properties, cb_fn, cb_arg, NULL);
}

void
scst_pipeline(const struct sto_pipeline_properties *properties,
	      sto_generic_cb cb_fn, void *cb_arg, void *priv)
{
	struct scst *scst = g_scst;

	sto_pipeline_alloc_and_run(scst->engine, properties, cb_fn, cb_arg, priv);
}

static void
init_scan_system_done(void *cb_arg, int rc)
{
	struct sto_generic_cpl *cpl = cb_arg;

	if (rc && rc != -ENOENT) {
		SPDK_ERRLOG("Failed to scan system, rc=%d\n", rc);
		goto out;
	}

	rc = 0;

	SPDK_ERRLOG("SCST has been successfully scanned\n");

out:
	sto_generic_call_cpl(cpl, rc);
}

void
scst_init(sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_generic_cpl *cpl;
	struct scst *scst;

	if (g_scst) {
		SPDK_ERRLOG("FAILED: SCST has already been initialized\n");
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	cpl = sto_generic_cpl_alloc(cb_fn, cb_arg);
	if (spdk_unlikely(!cpl)) {
		SPDK_ERRLOG("Failed to alloc generic completion\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	scst = scst_create();
	if (spdk_unlikely(!scst)) {
		SPDK_ERRLOG("Failed to create SCST instance\n");
		sto_generic_call_cpl(cpl, -ENOMEM);
		return;
	}

	g_scst = scst;

	scst_scan_system(init_scan_system_done, cpl);
}

void
scst_fini(sto_generic_cb cb_fn, void *cb_arg)
{
	struct scst *scst = g_scst;
	struct scst_device_handler *handler, *tmp;

	TAILQ_FOREACH_SAFE(handler, &scst->handler_list, list, tmp) {
		scst_device_handler_destroy(handler);
	}

	scst_destroy(scst);

	cb_fn(cb_arg, 0);
}
