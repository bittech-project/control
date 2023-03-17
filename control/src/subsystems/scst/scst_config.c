#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/queue.h>
#include <spdk/likely.h>
#include <spdk/log.h>
#include <spdk/string.h>

#include "scst_lib.h"
#include "scst.h"

#include "sto_json.h"
#include "sto_pipeline.h"
#include "sto_err.h"
#include "sto_rpc_aio.h"
#include "sto_tree.h"

struct scst_json_ctx {
	struct sto_json_ctx json;
};

static inline void
scst_json_ctx_deinit(void *ctx_ptr)
{
	struct scst_json_ctx *ctx = ctx_ptr;

	sto_json_ctx_destroy(&ctx->json);
}

static inline struct spdk_json_val *
handler_json_iter_next(struct spdk_json_val *json, struct spdk_json_val *object)
{
	return sto_json_array_next(json, object, "handlers");
}

static inline struct spdk_json_val *
device_json_iter_next(struct spdk_json_val *json, struct spdk_json_val *object)
{
	return sto_json_array_next(sto_json_value(spdk_json_object_first(json)), object, "devices");
}

static inline struct spdk_json_val *
driver_json_iter_next(struct spdk_json_val *json, struct spdk_json_val *object)
{
	return sto_json_array_next(json, object, "drivers");
}

static inline struct spdk_json_val *
target_json_iter_next(struct spdk_json_val *json, struct spdk_json_val *object)
{
	return sto_json_array_next(sto_json_value(spdk_json_object_first(json)), object, "targets");
}

static inline struct spdk_json_val *
ini_group_json_iter_next(struct spdk_json_val *json, struct spdk_json_val *object)
{
	return sto_json_array_next(sto_json_value(spdk_json_object_first(json)), object, "ini_groups");
}

static int
decode_target_params(struct spdk_json_val *driver, struct spdk_json_val *target,
		     struct scst_target_params *params)
{
	if (spdk_json_decode_string(spdk_json_object_first(driver), &params->driver_name)) {
		SPDK_ERRLOG("Failed to decode `driver_name`\n");
		goto out_err;
	}

	if (spdk_json_decode_string(spdk_json_object_first(target), &params->target_name)) {
		SPDK_ERRLOG("Failed to decode `target_name`\n");
		goto out_err;
	}

	return 0;

out_err:
	scst_target_params_deinit(params);
	return -EINVAL;
}

static int
decode_ini_group_params(struct spdk_json_val *driver, struct spdk_json_val *target,
			struct spdk_json_val *ini_group, struct scst_ini_group_params *params)
{
	if (spdk_json_decode_string(spdk_json_object_first(driver), &params->driver_name)) {
		SPDK_ERRLOG("Failed to decode `driver_name`\n");
		goto out_err;
	}

	if (spdk_json_decode_string(spdk_json_object_first(target), &params->target_name)) {
		SPDK_ERRLOG("Failed to decode `target_name`\n");
		goto out_err;
	}

	if (spdk_json_decode_string(spdk_json_object_first(ini_group), &params->ini_group_name)) {
		SPDK_ERRLOG("Failed to decode `ini_group_name`\n");
		goto out_err;
	}

	return 0;

out_err:
	scst_ini_group_params_deinit(params);
	return -EINVAL;

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
	struct sto_tree_node *device_node;

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

static void
lun_dumps_json(struct sto_tree_node *lun_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *device_lnk_node, *device_node;

	device_lnk_node = sto_tree_node_find(lun_node, "device");
	assert(device_lnk_node);

	device_node = sto_tree_node_resolv_lnk(device_lnk_node);
	if (spdk_unlikely(!device_node)) {
		SPDK_ERRLOG("Failed to resolve device %s link\n",
			    device_lnk_node->inode->name);
		return;
	}

	spdk_json_write_name(w, lun_node->inode->name);
	spdk_json_write_object_begin(w);

	scst_serialize_attrs(lun_node, w);

	spdk_json_write_named_string(w, "device", device_node->inode->name);

	spdk_json_write_object_end(w);
}

static void
lun_list_dumps_json(struct sto_tree_node *group_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *lun_list_node, *lun_node;

	lun_list_node = sto_tree_node_find(group_node, "luns");

	if (lun_list_node && sto_tree_node_first_child_type(lun_list_node, STO_INODE_TYPE_DIR)) {
		spdk_json_write_named_array_begin(w, "luns");

		STO_TREE_FOREACH_TYPE(lun_node, lun_list_node, STO_INODE_TYPE_DIR) {
			spdk_json_write_object_begin(w);
			lun_dumps_json(lun_node, w);
			spdk_json_write_object_end(w);
		}

		spdk_json_write_array_end(w);
	}
}

static void
ini_group_dumps_json(struct sto_tree_node *group_node, struct spdk_json_write_ctx *w)
{
	spdk_json_write_name(w, group_node->inode->name);
	spdk_json_write_object_begin(w);

	lun_list_dumps_json(group_node, w);

	spdk_json_write_object_end(w);
}

static void
ini_group_list_dumps_json(struct sto_tree_node *target_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *ini_group_list_node, *group_node;

	ini_group_list_node = sto_tree_node_find(target_node, "ini_groups");

	if (ini_group_list_node && sto_tree_node_first_child_type(ini_group_list_node, STO_INODE_TYPE_DIR)) {
		spdk_json_write_named_array_begin(w, "ini_groups");

		STO_TREE_FOREACH_TYPE(group_node, ini_group_list_node, STO_INODE_TYPE_DIR) {
			spdk_json_write_object_begin(w);
			ini_group_dumps_json(group_node, w);
			spdk_json_write_object_end(w);
		}

		spdk_json_write_array_end(w);
	}
}

static void
target_dumps_json(struct sto_tree_node *target_node, struct spdk_json_write_ctx *w)
{
	spdk_json_write_name(w, target_node->inode->name);

	spdk_json_write_object_begin(w);

	ini_group_list_dumps_json(target_node, w);
	lun_list_dumps_json(target_node, w);

	spdk_json_write_object_end(w);
}

static void
driver_dumps_json(struct sto_tree_node *driver, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *target_node;

	spdk_json_write_name(w, driver->inode->name);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "targets");

	STO_TREE_FOREACH_TYPE(target_node, driver, STO_INODE_TYPE_DIR) {
		spdk_json_write_object_begin(w);
		target_dumps_json(target_node, w);
		spdk_json_write_object_end(w);
	}

	spdk_json_write_array_end(w);

	spdk_json_write_object_end(w);
}

static bool
driver_list_is_empty(struct sto_tree_node *driver_list_node)
{
	struct sto_tree_node *driver_node;

	if (sto_tree_node_first_child_type(driver_list_node, STO_INODE_TYPE_DIR) == NULL) {
		return true;
	}

	STO_TREE_FOREACH_TYPE(driver_node, driver_list_node, STO_INODE_TYPE_DIR) {
		if (sto_tree_node_first_child_type(driver_node, STO_INODE_TYPE_DIR) != NULL) {
			return false;
		}
	}

	return true;
}

static void
driver_list_dumps_json(struct sto_tree_node *tree_root, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *driver_list_node, *driver_node;

	driver_list_node = sto_tree_node_find(tree_root, "targets");

	if (!driver_list_node || driver_list_is_empty(driver_list_node)) {
		return;
	}

	spdk_json_write_named_array_begin(w, "drivers");

	STO_TREE_FOREACH_TYPE(driver_node, driver_list_node, STO_INODE_TYPE_DIR) {
		if (sto_tree_node_first_child_type(driver_node, STO_INODE_TYPE_DIR) == NULL) {
			continue;
		}

		spdk_json_write_object_begin(w);
		driver_dumps_json(driver_node, w);
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
	driver_list_dumps_json(tree_root, w);

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

	sto_tree(SCST_ROOT, 0, false, dumps_json, ctx);
}

static void
device_load_json(struct sto_json_async_iter *iter)
{
	struct spdk_json_val *handler, *device;
	char *handler_name = NULL, *device_name = NULL;
	int rc = 0;

	handler = sto_json_async_iter_get_json(iter);
	device = sto_json_async_iter_get_object(iter);

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

	rc = scst_add_device(scst_get_instance(), handler_name, device_name);

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

static void
ini_group_load_json(struct sto_json_async_iter *iter)
{
	struct spdk_json_val *driver, *target, *ini_group;
	struct scst_ini_group_params params = {};
	int rc = 0;

	driver = sto_json_async_iter_get_json(sto_json_async_iter_get_priv(iter));
	target = sto_json_async_iter_get_json(iter);
	ini_group = sto_json_async_iter_get_object(iter);

	rc = decode_ini_group_params(driver, target, ini_group, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode ini_group params to load\n");
		goto out;
	}

	rc = scst_add_ini_group(scst_get_instance(), params.driver_name,
				params.target_name, params.ini_group_name);

out:
	scst_ini_group_params_deinit(&params);

	sto_json_async_iter_next(iter, rc);
}

static void
ini_group_list_load_json(struct sto_json_async_iter *iter)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_json_async_iter_get_object(iter),
		.iterate_fn = ini_group_load_json,
		.next_fn = ini_group_json_iter_next,
		.priv = iter,
	};

	sto_json_async_iter_start(&opts, sto_json_async_iterate_done, iter);
}

static void
target_load_json(struct sto_json_async_iter *iter)
{
	struct spdk_json_val *driver, *target;
	struct scst_target_params params = {};
	int rc = 0;

	driver = sto_json_async_iter_get_json(iter);
	target = sto_json_async_iter_get_object(iter);

	rc = decode_target_params(driver, target, &params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode target params\n");
		goto out_err;
	}

	rc = scst_add_target(scst_get_instance(), params.driver_name, params.target_name);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to add target\n");
		goto out_err;
	}

	ini_group_list_load_json(iter);

out:
	scst_target_params_deinit(&params);
	return;

out_err:
	sto_json_async_iter_next(iter, rc);
	goto out;
}

static void
driver_load_json(struct sto_json_async_iter *iter)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_json_async_iter_get_object(iter),
		.iterate_fn = target_load_json,
		.next_fn = target_json_iter_next,
	};

	sto_json_async_iter_start(&opts, sto_json_async_iterate_done, iter);
}

static void
driver_list_load_json_step(struct sto_pipeline *pipe)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_pipeline_get_priv(pipe),
		.iterate_fn = driver_load_json,
		.next_fn = driver_json_iter_next,
	};

	sto_json_async_iter_start(&opts, sto_pipeline_step_done, pipe);
}

static const struct sto_pipeline_properties scst_load_json_properties = {
	.steps = {
		STO_PL_STEP(handler_list_load_json_step, NULL),
		STO_PL_STEP(driver_list_load_json_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

static void
scst_load_json(struct spdk_json_val *json, sto_generic_cb cb_fn, void *cb_arg)
{
	sto_json_print("SCST load JSON", json);
	scst_pipeline(scst_get_instance(), &scst_load_json_properties, cb_fn, cb_arg, json);
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
	scst_pipeline(scst_get_instance(), &scst_scan_system_properties, cb_fn, cb_arg, NULL);
}

struct info_json_ctx {
	struct scst_device_handler *handler;
	struct scst_device *device;

	struct scst_target_driver *driver;
	struct scst_target *target;
	struct scst_ini_group *ini_group;
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
	const char *device_path;

	device_path = scst_device_path(ctx->device);
	if (spdk_unlikely(!device_path)) {
		SPDK_ERRLOG("Failed to alloc `%s` device path\n",
			    ctx->device->name);
		sto_pipeline_step_next(pipe, -ENOMEM);
		return;
	}

	scst_read_attrs(device_path, device_read_attrs_done, pipe);

	free((char *) device_path);
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

	ctx->handler = scst_device_handler_next(scst_get_instance(), ctx->handler);
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
	struct scst *scst = scst_get_instance();
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
ini_group_json_constructor(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	spdk_json_write_name(w, ctx->ini_group->name);
	spdk_json_write_object_begin(w);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	sto_pipeline_step_next(pipe, 0);
}

static int
ini_group_list_json_constructor(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	ctx->ini_group = scst_ini_group_next(ctx->target, ctx->ini_group);
	if (ctx->ini_group) {
		spdk_json_write_object_begin(w);

		sto_pipeline_queue_step(pipe, STO_PL_STEP(ini_group_json_constructor, NULL));
		return 0;
	}

	spdk_json_write_array_end(w);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	return STO_PL_CONSTRUCTOR_FINISHED;
}

static void
target_json_constructor(struct sto_pipeline *pipe)
{
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	int rc = 0;

	spdk_json_write_name(w, ctx->target->name);
	spdk_json_write_object_begin(w);

	if (TAILQ_EMPTY(&ctx->target->group_list)) {
		spdk_json_write_object_end(w);
		spdk_json_write_object_end(w);
		goto out;
	}

	spdk_json_write_named_array_begin(w, "ini_groups");

	rc = sto_pipeline_insert_step(pipe, STO_PL_STEP_CONSTRUCTOR(ini_group_list_json_constructor, NULL));

out:
	sto_pipeline_step_next(pipe, rc);
}

static int
target_list_json_constructor(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	ctx->target = scst_target_next(ctx->driver, ctx->target);
	if (ctx->target) {
		spdk_json_write_object_begin(w);

		sto_pipeline_queue_step(pipe, STO_PL_STEP(target_json_constructor, NULL));
		return 0;
	}

	spdk_json_write_array_end(w);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	return STO_PL_CONSTRUCTOR_FINISHED;
}

static int
driver_list_json_constructor(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct info_json_ctx *ctx = sto_pipeline_get_ctx(pipe);

	ctx->driver = scst_target_driver_next(scst_get_instance(), ctx->driver);
	if (ctx->driver) {
		spdk_json_write_object_begin(w);

		spdk_json_write_name(w, ctx->driver->name);
		spdk_json_write_object_begin(w);
		spdk_json_write_named_array_begin(w, "targets");

		sto_pipeline_queue_step(pipe, STO_PL_STEP_CONSTRUCTOR(target_list_json_constructor, NULL));
		return 0;
	}

	spdk_json_write_array_end(w);

	return STO_PL_CONSTRUCTOR_FINISHED;
}

static void
info_json_driver_list_step(struct sto_pipeline *pipe)
{
	struct spdk_json_write_ctx *w = sto_pipeline_get_priv(pipe);
	struct scst *scst = scst_get_instance();
	int rc = 0;

	if (TAILQ_EMPTY(&scst->driver_list)) {
		goto out;
	}

	spdk_json_write_named_array_begin(w, "drivers");

	rc = sto_pipeline_insert_step(pipe, STO_PL_STEP_CONSTRUCTOR(driver_list_json_constructor, NULL));

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
		STO_PL_STEP(info_json_driver_list_step, NULL),
		STO_PL_STEP(info_json_end_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

static void
scst_info_json(void *cb_ctx, struct spdk_json_write_ctx *w,
	       sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_info_json_properties, cb_fn, cb_arg, w);
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
	struct scst *scst = scst_get_instance();

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

void
scst_write_config(sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_write_config_properties, cb_fn, cb_arg, NULL);
}

static int
attr_add(struct sto_json_str_field *attr, char **attributes)
{
	char *attr_s, *tmp;
	int rc = 0;

	attr_s = spdk_sprintf_alloc("%s=%s", attr->name, attr->value);
	if (spdk_unlikely(!attr_s)) {
		SPDK_ERRLOG("Failed to alloc memory for parsed attributes\n");
		return -ENOMEM;
	}

	tmp = spdk_sprintf_append_realloc(*attributes, "%s,", attr_s);
	if (spdk_unlikely(!tmp)) {
		SPDK_ERRLOG("Failed to realloc memory for attributes\n");
		rc = -ENOMEM;
		goto out;
	}

	*attributes = tmp;

out:
	free(attr_s);

	return rc;
}

static int
parse_attr(struct sto_json_iter *iter, char **available_params, char **result)
{
	struct sto_json_str_field attr = {};
	int rc;

	rc = sto_json_iter_decode_str_field(iter, &attr);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode dev attribute\n");
		return rc;
	}

	if (available_params &&
			!scst_available_attrs_find(available_params, attr.name)) {
		goto out;
	}

	rc = attr_add(&attr, result);

out:
	sto_json_str_field_destroy(&attr);

	return rc;
}

static char *
scst_parse_attrs(struct spdk_json_val *attributes, char **available_params)
{
	const struct spdk_json_val *val;
	char *attributes_str = NULL;
	struct sto_json_iter iter;
	int rc = 0;

	if (!attributes) {
		return NULL;
	}

	STO_JSON_FOREACH(val, attributes, &iter) {
		rc = parse_attr(&iter, available_params, &attributes_str);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to parse attribute, rc=%d\n", rc);
			goto out_err;
		}
	}

	if (!attributes_str) {
		return NULL;
	}

	attributes_str[strlen(attributes_str) - 1] = '\0';

	return attributes_str;

out_err:
	free(attributes_str);

	return ERR_PTR(rc);
}

struct device_restore_ctx {
	struct scst_device_params params;
	struct sto_json_async_iter *iter;
};

static void
device_restore_json_done(void *cb_arg, int rc)
{
	struct device_restore_ctx *ctx = cb_arg;

	if (rc && rc == -EEXIST) {
		SPDK_ERRLOG("Device %s has been alredy restored\n",
			    ctx->params.device_name);
		rc = 0;
	}

	sto_json_async_iter_next(ctx->iter, rc);

	scst_device_params_deinit(&ctx->params);
	free(ctx);
}

static void
device_restore_json(struct sto_json_async_iter *iter)
{
	struct spdk_json_val *handler, *device, *values;
	struct device_restore_ctx *ctx;
	struct scst_device_params *params;
	char **available_params;
	int rc = 0;

	handler = sto_json_async_iter_get_json(iter);
	device = sto_json_async_iter_get_object(iter);
	values = sto_json_value(spdk_json_object_first(device));

	available_params = sto_json_async_iter_get_priv(iter);

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc ctx for device restore\n");
		sto_json_async_iter_next(iter, -ENOMEM);
		return;
	}

	ctx->iter = iter;
	params = &ctx->params;

	if (spdk_json_decode_string(spdk_json_object_first(handler), &params->handler_name)) {
		SPDK_ERRLOG("Failed to decode handler name\n");
		rc = -EINVAL;
		goto out_err;
	}

	if (spdk_json_decode_string(spdk_json_object_first(device), &params->device_name)) {
		SPDK_ERRLOG("Failed to decode device name\n");
		rc = -EINVAL;
		goto out_err;
	}

	if (values) {
		char *attributes = scst_parse_attrs(values, available_params);

		if (IS_ERR(attributes)) {
			SPDK_ERRLOG("Failed to parse SCST attributes\n");
			rc = -EINVAL;
			goto out_err;
		}

		params->attributes = attributes;
	}

	scst_device_open(params, device_restore_json_done, ctx);

	return;

out_err:
	device_restore_json_done(ctx, rc);
}

struct handler_restore_ctx {
	char **available_params;
};

static inline void
handler_restore_ctx_deinit(void *ctx_ptr)
{
	struct handler_restore_ctx *ctx = ctx_ptr;

	scst_available_attrs_destroy(ctx->available_params);
}

static void
handler_restore_devices_step(struct sto_pipeline *pipe)
{
	struct handler_restore_ctx *ctx = sto_pipeline_get_ctx(pipe);
	struct sto_json_async_iter_opts opts = {
		.json = sto_json_async_iter_get_object(sto_pipeline_get_priv(pipe)),
		.iterate_fn = device_restore_json,
		.next_fn = device_json_iter_next,
		.priv = ctx->available_params,
	};

	sto_json_async_iter_start(&opts, sto_pipeline_step_done, pipe);
}

static const char *
handler_parse_mgmt_path(struct spdk_json_val *handler)
{
	char *handler_name = NULL;
	const char *mgmt_path = NULL;

	if (spdk_json_decode_string(spdk_json_object_first(handler), &handler_name)) {
		SPDK_ERRLOG("Failed to decode handler name\n");
		return ERR_PTR(-EINVAL);
	}

	mgmt_path = scst_device_handler_mgmt_path(handler_name);

	free(handler_name);

	if (spdk_unlikely(!mgmt_path)) {
		SPDK_ERRLOG("Failed to alloc handler mgmt path\n");
		return ERR_PTR(-ENOMEM);
	}

	return mgmt_path;
}

static void
handler_read_available_attrs_step(struct sto_pipeline *pipe)
{
	struct handler_restore_ctx *ctx = sto_pipeline_get_ctx(pipe);
	struct spdk_json_val *handler = sto_json_async_iter_get_object(sto_pipeline_get_priv(pipe));
	const char *mgmt_path = NULL;

	mgmt_path = handler_parse_mgmt_path(handler);
	if (IS_ERR(mgmt_path)) {
		SPDK_ERRLOG("Failed to parse handler mgmt path\n");
		sto_pipeline_step_next(pipe, PTR_ERR(mgmt_path));
		return;
	}

	scst_read_available_attrs(mgmt_path, "The following parameters available:",
				  sto_pipeline_step_done, pipe, &ctx->available_params);

	free((char *) mgmt_path);
}

static const struct sto_pipeline_properties handler_restore_json_properties = {
	.ctx_size = sizeof(struct handler_restore_ctx),
	.ctx_deinit_fn = handler_restore_ctx_deinit,

	.steps = {
		STO_PL_STEP(handler_read_available_attrs_step, NULL),
		STO_PL_STEP(handler_restore_devices_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

static void
handler_restore_json(struct sto_json_async_iter *iter)
{
	scst_pipeline(scst_get_instance(), &handler_restore_json_properties,
		      sto_json_async_iterate_done, iter, iter);
}

static void
handler_list_restore_json_step(struct sto_pipeline *pipe)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_pipeline_get_priv(pipe),
		.iterate_fn = handler_restore_json,
		.next_fn = handler_json_iter_next,
	};

	sto_json_async_iter_start(&opts, sto_pipeline_step_done, pipe);
}

struct ini_group_restore_ctx {
	struct scst_ini_group_params params;
	struct sto_json_async_iter *iter;
};

static void
ini_group_restore_json_done(void *cb_arg, int rc)
{
	struct ini_group_restore_ctx *ctx = cb_arg;

	switch (rc) {
	case -EEXIST:
		SPDK_ERRLOG("Ini group %s has been alredy restored\n",
			    ctx->params.ini_group_name);
		/* fallthrough */
	case 0:
		break;
	default:
		sto_json_async_iter_next(ctx->iter, rc);
		goto free_ctx;
	}

	sto_json_async_iter_next(ctx->iter, 0);

free_ctx:
	scst_ini_group_params_deinit(&ctx->params);
	free(ctx);
}

static void
ini_group_restore_json(struct sto_json_async_iter *iter)
{
	struct spdk_json_val *driver, *target, *ini_group;
	struct ini_group_restore_ctx *ctx;
	int rc = 0;

	driver = sto_json_async_iter_get_json(sto_json_async_iter_get_priv(iter));
	target = sto_json_async_iter_get_json(iter);
	ini_group = sto_json_async_iter_get_object(iter);

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc ctx for ini_group restore\n");
		sto_json_async_iter_next(iter, -ENOMEM);
		return;
	}

	ctx->iter = iter;

	rc = decode_ini_group_params(driver, target, ini_group, &ctx->params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode ini_group params to load\n");
		goto out;
	}

	scst_ini_group_add(&ctx->params, ini_group_restore_json_done, ctx);

	return;

out:
	ini_group_restore_json_done(ctx, rc);
}

static void
ini_group_list_restore_json(struct sto_json_async_iter *iter)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_json_async_iter_get_object(iter),
		.iterate_fn = ini_group_restore_json,
		.next_fn = ini_group_json_iter_next,
		.priv = iter,
	};

	sto_json_async_iter_start(&opts, sto_json_async_iterate_done, iter);
}

struct target_restore_ctx {
	struct scst_target_params params;
	struct sto_json_async_iter *iter;
};

static void
target_restore_json_done(void *cb_arg, int rc)
{
	struct target_restore_ctx *ctx = cb_arg;

	switch (rc) {
	case -EEXIST:
		SPDK_ERRLOG("Target %s has been alredy restored\n",
			    ctx->params.target_name);
		/* fallthrough */
	case 0:
		break;
	default:
		sto_json_async_iter_next(ctx->iter, rc);
		goto free_ctx;
	}

	ini_group_list_restore_json(ctx->iter);

free_ctx:
	scst_target_params_deinit(&ctx->params);
	free(ctx);
}

static void
target_restore_json(struct sto_json_async_iter *iter)
{
	struct spdk_json_val *driver, *target;
	struct target_restore_ctx *ctx;
	int rc = 0;

	driver = sto_json_async_iter_get_json(iter);
	target = sto_json_async_iter_get_object(iter);

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc ctx for target restore\n");
		sto_json_async_iter_next(iter, -ENOMEM);
		return;
	}

	ctx->iter = iter;

	rc = decode_target_params(driver, target, &ctx->params);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to decode target params\n");
		goto out_err;
	}

	scst_target_add(&ctx->params, target_restore_json_done, ctx);

	return;

out_err:
	target_restore_json_done(ctx, rc);
}

static void
driver_restore_json(struct sto_json_async_iter *iter)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_json_async_iter_get_object(iter),
		.iterate_fn = target_restore_json,
		.next_fn = target_json_iter_next,
	};

	sto_json_async_iter_start(&opts, sto_json_async_iterate_done, iter);
}

static void
driver_list_restore_json_step(struct sto_pipeline *pipe)
{
	struct sto_json_async_iter_opts opts = {
		.json = sto_pipeline_get_priv(pipe),
		.iterate_fn = driver_restore_json,
		.next_fn = driver_json_iter_next,
	};

	sto_json_async_iter_start(&opts, sto_pipeline_step_done, pipe);
}

static const struct sto_pipeline_properties scst_restore_json_properties = {
	.steps = {
		STO_PL_STEP(handler_list_restore_json_step, NULL),
		STO_PL_STEP(driver_list_restore_json_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

static void
scst_restore_json(struct spdk_json_val *json, sto_generic_cb cb_fn, void *cb_arg)
{
	sto_json_print("SCST restore JSON", json);
	scst_pipeline(scst_get_instance(), &scst_restore_json_properties, cb_fn, cb_arg, json);
}

static void
restore_config_read_step(struct sto_pipeline *pipe)
{
	struct scst_json_ctx *ctx = sto_pipeline_get_ctx(pipe);
	struct scst *scst = scst_get_instance();

	sto_rpc_readfile_buf(scst->config_path, 0,
			     sto_pipeline_step_done, pipe,
			     (char **) &ctx->json.buf);
}

static void
restore_config_parse_step(struct sto_pipeline *pipe)
{
	struct scst_json_ctx *ctx = sto_pipeline_get_ctx(pipe);
	struct sto_json_ctx *json = &ctx->json;
	int rc;

	json->size = strlen(json->buf);

	rc = sto_json_ctx_parse(json);
	if (spdk_unlikely(rc)) {
		sto_pipeline_step_next(pipe, rc);
		return;
	}

	scst_restore_json((struct spdk_json_val *) json->values, sto_pipeline_step_done, pipe);
}

static const struct sto_pipeline_properties scst_restore_config_properties = {
	.ctx_size = sizeof(struct scst_json_ctx),
	.ctx_deinit_fn = scst_json_ctx_deinit,

	.steps = {
		STO_PL_STEP(restore_config_read_step, NULL),
		STO_PL_STEP(restore_config_parse_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_restore_config(sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_restore_config_properties, cb_fn, cb_arg, NULL);
}
