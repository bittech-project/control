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


static struct sto_rpc_writefile_args *
device_open_create_args(const char *handler_name, const char *device_name, const char *attributes)
{
	struct sto_rpc_writefile_args *args;

	args = calloc(1, sizeof(*args));
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to alloc writefile args\n");
		return NULL;
	}

	args->filepath = scst_device_handler_mgmt_path(handler_name);
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

	args->filepath = scst_device_handler_mgmt_path(handler_name);
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
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);

	if (scst_find_device(scst_get_instance(), params->handler_name, params->device_name)) {
		sto_pipeline_step_next(pipe, -EEXIST);
		return;
	}

	device_open(params->handler_name, params->device_name, params->attributes,
		    sto_pipeline_step_done, pipe);
}

static void
device_open_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);

	device_close(params->handler_name, params->device_name, sto_pipeline_step_done, pipe);
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

	if (!scst_find_device(scst_get_instance(), params->handler_name, params->device_name)) {
		sto_pipeline_step_next(pipe, -ENOENT);
		return;
	}

	device_close(params->handler_name, params->device_name, sto_pipeline_step_done, pipe);
}

static void
device_close_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_remove_device(scst_get_instance(), params->handler_name, params->device_name);
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

static struct sto_rpc_writefile_args *
target_add_create_args(const char *driver_name, const char *target_name)
{
	struct sto_rpc_writefile_args *args;

	args = calloc(1, sizeof(*args));
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to alloc writefile args\n");
		return NULL;
	}

	args->filepath = scst_target_driver_mgmt_path(driver_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("add_target %s", target_name);
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
target_add(const char *driver_name, const char *target_name,
	   sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args *args;

	args = target_add_create_args(driver_name, target_name);
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to create writefile args for `target_add`\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	SPDK_ERRLOG("SCST target add: filepath[%s], buf[%s]\n", args->filepath, args->buf);

	sto_rpc_writefile_args(args, 0, cb_fn, cb_arg);
}

static struct sto_rpc_writefile_args *
target_del_create_args(const char *driver_name, const char *target_name)
{
	struct sto_rpc_writefile_args *args;

	args = calloc(1, sizeof(*args));
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to alloc writefile args\n");
		return NULL;
	}

	args->filepath = scst_target_driver_mgmt_path(driver_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("del_target %s", target_name);
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
target_del(const char *driver_name, const char *target_name,
	   sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args *args;

	args = target_del_create_args(driver_name, target_name);
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to create writefile args for `target_del`\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	SPDK_ERRLOG("SCST target del: filepath[%s], data[%s]\n", args->filepath, args->buf);

	sto_rpc_writefile_args(args, 0, cb_fn, cb_arg);
}

static void
target_add_step(struct sto_pipeline *pipe)
{
	struct scst_target_params *params = sto_pipeline_get_priv(pipe);

	if (scst_find_target(scst_get_instance(), params->driver_name, params->target_name)) {
		sto_pipeline_step_next(pipe, -EEXIST);
		return;
	}

	target_add(params->driver_name, params->target_name, sto_pipeline_step_done, pipe);
}

static void
target_add_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_target_params *params = sto_pipeline_get_priv(pipe);

	target_del(params->driver_name, params->target_name, sto_pipeline_step_done, pipe);
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

	target_del(params->driver_name, params->target_name, sto_pipeline_step_done, pipe);
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

static struct sto_rpc_writefile_args *
ini_group_add_create_args(const char *driver_name, const char *target_name, const char *ini_group_name)
{
	struct sto_rpc_writefile_args *args;

	args = calloc(1, sizeof(*args));
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to alloc writefile args\n");
		return NULL;
	}

	args->filepath = scst_ini_group_mgmt_path(driver_name, target_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("create %s", ini_group_name);
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
ini_group_add(const char *driver_name, const char *target_name, const char *ini_group_name,
	      sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args *args;

	args = ini_group_add_create_args(driver_name, target_name, ini_group_name);
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to create writefile args for `ini_group_add`\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	SPDK_ERRLOG("SCST ini_group add: filepath[%s], buf[%s]\n", args->filepath, args->buf);

	sto_rpc_writefile_args(args, 0, cb_fn, cb_arg);
}

static struct sto_rpc_writefile_args *
ini_group_del_create_args(const char *driver_name, const char *target_name, const char *ini_group_name)
{
	struct sto_rpc_writefile_args *args;

	args = calloc(1, sizeof(*args));
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to alloc writefile args\n");
		return NULL;
	}

	args->filepath = scst_ini_group_mgmt_path(driver_name, target_name);
	if (spdk_unlikely(!args->filepath)) {
		SPDK_ERRLOG("Failed to alloc writefile args filepath\n");
		goto out_err;
	}

	args->buf = spdk_sprintf_alloc("del %s", ini_group_name);
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
ini_group_del(const char *driver_name, const char *target_name, const char *ini_group_name,
	      sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_rpc_writefile_args *args;

	args = ini_group_del_create_args(driver_name, target_name, ini_group_name);
	if (spdk_unlikely(!args)) {
		SPDK_ERRLOG("Failed to create writefile args for `ini_group_del`\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	SPDK_ERRLOG("SCST ini_group del: filepath[%s], data[%s]\n", args->filepath, args->buf);

	sto_rpc_writefile_args(args, 0, cb_fn, cb_arg);
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

	ini_group_add(params->driver_name, params->target_name, params->ini_group_name,
		      sto_pipeline_step_done, pipe);
}

static void
ini_group_add_rollback_step(struct sto_pipeline *pipe)
{
	struct scst_ini_group_params *params = sto_pipeline_get_priv(pipe);

	ini_group_del(params->driver_name, params->target_name, params->ini_group_name,
		      sto_pipeline_step_done, pipe);
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

	ini_group_del(params->driver_name, params->target_name, params->ini_group_name,
		      sto_pipeline_step_done, pipe);
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
