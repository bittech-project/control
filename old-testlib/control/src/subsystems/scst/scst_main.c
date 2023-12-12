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

static struct scst *g_scst;

void
scst_device_params_deinit(void *params_ptr)
{
	struct scst_device_params *params = params_ptr;

	free(params->handler_name);
	params->handler_name = NULL;

	free(params->device_name);
	params->device_name = NULL;

	free(params->attributes);
	params->attributes = NULL;
}

static int
device_open_init_args(struct scst_device_params *params, struct sto_rpc_writefile_args *args)
{
	args->filepath = scst_device_handler_mgmt_path(params->handler_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("add_device %s", params->device_name);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	if (params->attributes) {
		char *tmp;

		tmp = spdk_sprintf_append_realloc(args->buf, " %s", params->attributes);
		if (spdk_unlikely(!tmp)) {
			SPDK_ERRLOG("Failed to realloc writefile args buf\n");
			goto out_err;
		}

		args->buf = tmp;
	}

	return 0;

out_err:
	sto_rpc_writefile_args_deinit(args);

	return -ENOMEM;
}

static void
device_open(struct scst_device_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args args = {};
	int rc;

	rc = device_open_init_args(params, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create writefile args for `device_open`\n");
		cb_fn(cb_arg, rc);
		return;
	}

	SPDK_ERRLOG("SCST device open, filepath[%s], buf[%s]\n", args.filepath, args.buf);

	sto_rpc_writefile_args(&args, cb_fn, cb_arg);
}

static int
device_close_init_args(struct scst_device_params *params, struct sto_rpc_writefile_args *args)
{
	args->filepath = scst_device_handler_mgmt_path(params->handler_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("del_device %s", params->device_name);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	return 0;

out_err:
	sto_rpc_writefile_args_deinit(args);

	return -ENOMEM;
}

static void
device_close(struct scst_device_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args args = {};
	int rc;

	rc = device_close_init_args(params, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create writefile args for `device_close`\n");
		cb_fn(cb_arg, rc);
		return;
	}

	SPDK_ERRLOG("SCST device close, filepath[%s], data[%s]\n", args.filepath, args.buf);

	sto_rpc_writefile_args(&args, cb_fn, cb_arg);
}

static void
device_open_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);

	if (scst_find_device(scst_get_instance(), params->device_name)) {
		sto_pipeline_step_next(pipe, -EEXIST);
		return;
	}

	device_open(params, sto_pipeline_step_done, pipe);
}

static void
device_open_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);

	device_close(params, sto_pipeline_step_done, pipe);
}

static void
device_open_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_add_device(scst_get_instance(), params->handler_name, params->device_name);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_device_open_properties = {
	.steps = {
		STO_PL_STEP(device_open_step, device_open_rollback_step),
		STO_PL_STEP(device_open_cfg_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_device_open(struct scst_device_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_device_open_properties, cb_fn, cb_arg, params);
}

static void
device_close_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);

	if (!scst_find_device(scst_get_instance(), params->device_name)) {
		sto_pipeline_step_next(pipe, -ENOENT);
		return;
	}

	device_close(params, sto_pipeline_step_done, pipe);
}

static void
device_close_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_remove_device(scst_get_instance(), params->device_name);
	assert(!rc);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_device_close_properties = {
	.steps = {
		STO_PL_STEP(device_close_step, NULL),
		STO_PL_STEP(device_close_cfg_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_device_close(struct scst_device_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_device_close_properties, cb_fn, cb_arg, params);
}

void
scst_target_params_deinit(void *params_ptr)
{
	struct scst_target_params *params = params_ptr;

	free(params->driver_name);
	params->driver_name = NULL;

	free(params->target_name);
	params->target_name = NULL;
}

static int
target_add_init_args(struct scst_target_params *params, struct sto_rpc_writefile_args *args)
{
	args->filepath = scst_target_driver_mgmt_path(params->driver_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("add_target %s", params->target_name);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	return 0;

out_err:
	sto_rpc_writefile_args_deinit(args);

	return -ENOMEM;
}

static void
target_add(struct scst_target_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args args = {};
	int rc;

	rc = target_add_init_args(params, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create writefile args for `target_add`\n");
		cb_fn(cb_arg, rc);
		return;
	}

	SPDK_ERRLOG("SCST target add: filepath[%s], buf[%s]\n", args.filepath, args.buf);

	sto_rpc_writefile_args(&args, cb_fn, cb_arg);
}

static int
target_del_init_args(struct scst_target_params *params, struct sto_rpc_writefile_args *args)
{
	args->filepath = scst_target_driver_mgmt_path(params->driver_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("del_target %s", params->target_name);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	return 0;

out_err:
	sto_rpc_writefile_args_deinit(args);

	return -ENOMEM;
}

static void
target_del(struct scst_target_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args args = {};
	int rc;

	rc = target_del_init_args(params, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create writefile args for `target_del`\n");
		cb_fn(cb_arg, rc);
		return;
	}

	SPDK_ERRLOG("SCST target del: filepath[%s], data[%s]\n", args.filepath, args.buf);

	sto_rpc_writefile_args(&args, cb_fn, cb_arg);
}

static void
target_add_step(struct sto_pipeline *pipe)
{
	struct scst_target_params *params = sto_pipeline_get_priv(pipe);

	if (scst_find_target(scst_get_instance(), params->driver_name, params->target_name)) {
		sto_pipeline_step_next(pipe, -EEXIST);
		return;
	}

	target_add(params, sto_pipeline_step_done, pipe);
}

static void
target_add_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_target_params *params = sto_pipeline_get_priv(pipe);

	target_del(params, sto_pipeline_step_done, pipe);
}

static void
target_add_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_target_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_add_target(scst_get_instance(), params->driver_name, params->target_name);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_target_add_properties = {
	.steps = {
		STO_PL_STEP(target_add_step, target_add_rollback_step),
		STO_PL_STEP(target_add_cfg_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_target_add(struct scst_target_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_target_add_properties, cb_fn, cb_arg, params);
}

static void
target_del_step(struct sto_pipeline *pipe)
{
	struct scst_target_params *params = sto_pipeline_get_priv(pipe);

	if (!scst_find_target(scst_get_instance(), params->driver_name, params->target_name)) {
		sto_pipeline_step_next(pipe, -ENOENT);
		return;
	}

	target_del(params, sto_pipeline_step_done, pipe);
}

static void
target_del_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_target_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_remove_target(scst_get_instance(), params->driver_name, params->target_name);
	assert(!rc);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_target_del_properties = {
	.steps = {
		STO_PL_STEP(target_del_step, NULL),
		STO_PL_STEP(target_del_cfg_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_target_del(struct scst_target_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_target_del_properties, cb_fn, cb_arg, params);
}

void
scst_ini_group_params_deinit(void *params_ptr)
{
	struct scst_ini_group_params *params = params_ptr;

	free(params->driver_name);
	params->driver_name = NULL;

	free(params->target_name);
	params->target_name = NULL;

	free(params->ini_group_name);
	params->ini_group_name = NULL;
}

static int
ini_group_add_init_args(struct scst_ini_group_params *params, struct sto_rpc_writefile_args *args)
{
	args->filepath = scst_ini_group_mgmt_path(params->driver_name, params->target_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("create %s", params->ini_group_name);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	return 0;

out_err:
	sto_rpc_writefile_args_deinit(args);

	return -ENOMEM;
}

static void
ini_group_add(struct scst_ini_group_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args args = {};
	int rc;

	rc = ini_group_add_init_args(params, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create writefile args for `ini_group_add`\n");
		cb_fn(cb_arg, rc);
		return;
	}

	SPDK_ERRLOG("SCST ini_group add: filepath[%s], buf[%s]\n", args.filepath, args.buf);

	sto_rpc_writefile_args(&args, cb_fn, cb_arg);
}

static int
ini_group_del_init_args(struct scst_ini_group_params *params, struct sto_rpc_writefile_args *args)
{
	args->filepath = scst_ini_group_mgmt_path(params->driver_name, params->target_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("del %s", params->ini_group_name);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	return 0;

out_err:
	sto_rpc_writefile_args_deinit(args);

	return -ENOMEM;
}

static void
ini_group_del(struct scst_ini_group_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args args = {};
	int rc;

	rc = ini_group_del_init_args(params, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create writefile args for `ini_group_del`\n");
		cb_fn(cb_arg, rc);
		return;
	}

	SPDK_ERRLOG("SCST ini_group del: filepath[%s], data[%s]\n", args.filepath, args.buf);

	sto_rpc_writefile_args(&args, cb_fn, cb_arg);
}

static void
ini_group_add_step(struct sto_pipeline *pipe)
{
	struct scst_ini_group_params *params = sto_pipeline_get_priv(pipe);

	if (scst_find_ini_group(scst_get_instance(), params->driver_name,
				params->target_name, params->ini_group_name)) {
		sto_pipeline_step_next(pipe, -EEXIST);
		return;
	}

	ini_group_add(params, sto_pipeline_step_done, pipe);
}

static void
ini_group_add_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_ini_group_params *params = sto_pipeline_get_priv(pipe);

	ini_group_del(params, sto_pipeline_step_done, pipe);
}

static void
ini_group_add_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_ini_group_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_add_ini_group(scst_get_instance(), params->driver_name,
				params->target_name, params->ini_group_name);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_ini_group_add_properties = {
	.steps = {
		STO_PL_STEP(ini_group_add_step, ini_group_add_rollback_step),
		STO_PL_STEP(ini_group_add_cfg_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_ini_group_add(struct scst_ini_group_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_ini_group_add_properties, cb_fn, cb_arg, params);
}

static void
ini_group_del_step(struct sto_pipeline *pipe)
{
	struct scst_ini_group_params *params = sto_pipeline_get_priv(pipe);

	if (!scst_find_ini_group(scst_get_instance(), params->driver_name,
				 params->target_name, params->ini_group_name)) {
		sto_pipeline_step_next(pipe, -ENOENT);
		return;
	}

	ini_group_del(params, sto_pipeline_step_done, pipe);
}

static void
ini_group_del_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_ini_group_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_remove_ini_group(scst_get_instance(), params->driver_name,
				   params->target_name, params->ini_group_name);
	assert(!rc);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_ini_group_del_properties = {
	.steps = {
		STO_PL_STEP(ini_group_del_step, NULL),
		STO_PL_STEP(ini_group_del_cfg_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_ini_group_del(struct scst_ini_group_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_ini_group_del_properties, cb_fn, cb_arg, params);
}

void
scst_lun_params_deinit(void *params_ptr)
{
	struct scst_lun_params *params = params_ptr;

	free(params->driver_name);
	params->driver_name = NULL;

	free(params->target_name);
	params->target_name = NULL;

	free(params->ini_group_name);
	params->ini_group_name = NULL;

	free(params->device_name);
	params->device_name = NULL;

	free(params->attributes);
	params->attributes = NULL;
}

static int
lun_add_init_args(struct scst_lun_params *params, struct sto_rpc_writefile_args *args)
{
	args->filepath = scst_target_lun_mgmt_path(params->driver_name, params->target_name,
						   params->ini_group_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("add %s %u", params->device_name, params->lun_id);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	if (params->attributes) {
		char *result;

		result = spdk_sprintf_append_realloc(args->buf, " %s", params->attributes);
		if (spdk_unlikely(!result)) {
			SPDK_ERRLOG("Failed to realloc writefile args buf\n");
			goto out_err;
		}

		args->buf = result;
	}

	return 0;

out_err:
	sto_rpc_writefile_args_deinit(args);

	return -ENOMEM;
}

static void
lun_add(struct scst_lun_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args args = {};
	int rc;

	rc = lun_add_init_args(params, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create writefile args for `lun_add`\n");
		cb_fn(cb_arg, rc);
		return;
	}

	SPDK_ERRLOG("SCST lun add, filepath[%s], buf[%s]\n", args.filepath, args.buf);

	sto_rpc_writefile_args(&args, cb_fn, cb_arg);
}

static int
lun_del_init_args(struct scst_lun_params *params, struct sto_rpc_writefile_args *args)
{
	args->filepath = scst_target_lun_mgmt_path(params->driver_name,
						   params->target_name, params->ini_group_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("del %u", params->lun_id);
	if (spdk_unlikely(!args->buf)) {
		SPDK_ERRLOG("Failed to alloc writefile args buf\n");
		goto out_err;
	}

	return 0;

out_err:
	sto_rpc_writefile_args_deinit(args);
	return -ENOMEM;
}

static void
lun_del(struct scst_lun_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args args = {};
	int rc;

	rc = lun_del_init_args(params, &args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to create writefile args for `device_close`\n");
		cb_fn(cb_arg, rc);
		return;
	}

	SPDK_ERRLOG("SCST lun del, filepath[%s], data[%s]\n", args.filepath, args.buf);

	sto_rpc_writefile_args(&args, cb_fn, cb_arg);
}

static void
lun_add_step(struct sto_pipeline *pipe)
{
	struct scst_lun_params *params = sto_pipeline_get_priv(pipe);

	if (scst_find_lun(scst_get_instance(), params->driver_name, params->target_name,
			  params->ini_group_name, params->lun_id)) {
		sto_pipeline_step_next(pipe, -EEXIST);
		return;
	}

	lun_add(params, sto_pipeline_step_done, pipe);
}

static void
lun_add_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_lun_params *params = sto_pipeline_get_priv(pipe);

	lun_del(params, sto_pipeline_step_done, pipe);
}

static void
lun_add_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_lun_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_add_lun(scst_get_instance(), params->driver_name, params->target_name,
			  params->ini_group_name, params->device_name, params->lun_id);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_lun_add_properties = {
	.steps = {
		STO_PL_STEP(lun_add_step, lun_add_rollback_step),
		STO_PL_STEP(lun_add_cfg_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_lun_add(struct scst_lun_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_lun_add_properties, cb_fn, cb_arg, params);
}

static void
lun_del_step(struct sto_pipeline *pipe)
{
	struct scst_lun_params *params = sto_pipeline_get_priv(pipe);

	if (!scst_find_lun(scst_get_instance(), params->driver_name, params->target_name,
			   params->ini_group_name, params->lun_id)) {
		sto_pipeline_step_next(pipe, -ENOENT);
		return;
	}

	lun_del(params, sto_pipeline_step_done, pipe);
}

static void
lun_del_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_lun_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_remove_lun(scst_get_instance(), params->driver_name, params->target_name,
			     params->ini_group_name, params->lun_id);
	assert(!rc);

	sto_pipeline_step_next(pipe, rc);
}

static const struct sto_pipeline_properties scst_lun_del_properties = {
	.steps = {
		STO_PL_STEP(lun_del_step, NULL),
		STO_PL_STEP(lun_del_cfg_step, NULL),
		STO_PL_STEP_TERMINATOR(),
	},
};

void
scst_lun_del(struct scst_lun_params *params, sto_generic_cb cb_fn, void *cb_arg)
{
	scst_pipeline(scst_get_instance(), &scst_lun_del_properties, cb_fn, cb_arg, params);
}

static void
init_restore_config_done(void *cb_arg, int rc)
{
	struct sto_generic_cpl *cpl = cb_arg;

	if (rc && rc != -ENOENT) {
		SPDK_ERRLOG("Failed to restore config, rc=%d\n", rc);
		goto out;
	}

	rc = 0;

	SPDK_ERRLOG("SCST initialization successed finished\n");

out:
	sto_generic_call_cpl(cpl, rc);
}

static void
init_scan_system_done(void *cb_arg, int rc)
{
	struct sto_generic_cpl *cpl = cb_arg;

	if (rc && rc != -ENOENT) {
		SPDK_ERRLOG("Failed to scan system, rc=%d\n", rc);
		sto_generic_call_cpl(cpl, rc);
		return;
	}

	scst_restore_config(init_restore_config_done, cpl);
}

void
scst_init(sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_generic_cpl *cpl;
	struct scst *scst;

	SPDK_ERRLOG("SCST initialization has been started\n");

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

	scst_destroy(scst);

	cb_fn(cb_arg, 0);
}

struct scst *
scst_get_instance(void)
{
	return g_scst;
}
