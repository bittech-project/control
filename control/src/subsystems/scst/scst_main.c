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

static struct scst *g_scst;


static void scst_device_free(struct scst_device *device);
static void scst_device_destroy(struct scst_device *device);

static void scst_device_handler_free(struct scst_device_handler *handler);

static void scst_target_free(struct scst_target *target);
static void scst_target_destroy(struct scst_target *target);

static void scst_target_driver_free(struct scst_target_driver *driver);

static void scst_put_device_handler(struct scst_device_handler *handler);
static int scst_remove_device(const char *handler_name, const char *device_name);
static struct scst_device *scst_find_device(const char *handler_name, const char *device_name);

static void scst_put_target_driver(struct scst_target_driver *driver);
static int scst_remove_target(const char *driver_name, const char *target_name);
static struct scst_target *scst_find_target(const char *driver_name, const char *target_name);


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

	TAILQ_INIT(&handler->device_list);

	return handler;

out_err:
	scst_device_handler_free(handler);

	return NULL;
}

static void
scst_device_handler_free(struct scst_device_handler *handler)
{
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

struct scst_device_handler *
scst_device_handler_next(struct scst_device_handler *handler)
{
	struct scst *scst = scst_get_instance();

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

struct scst_device *
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
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);

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
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);

	device_close(params->handler_name, params->device_name, sto_pipeline_step_done, pipe);
}

static void
device_open_cfg_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);
	int rc;

	rc = scst_add_device(params->handler_name, params->device_name);

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
	scst_pipeline(&scst_device_open_properties, cb_fn, cb_arg, params);
}

static void
device_close_step(struct sto_pipeline *pipe)
{
	struct scst_device_params *params = sto_pipeline_get_priv(pipe);

	if (!scst_find_device(params->handler_name, params->device_name)) {
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

	rc = scst_remove_device(params->handler_name, params->device_name);
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
	scst_pipeline(&scst_device_close_properties, cb_fn, cb_arg, params);
}

static struct scst_target_driver *
scst_target_driver_alloc(const char *driver_name)
{
	struct scst_target_driver *driver;

	driver = calloc(1, sizeof(*driver));
	if (spdk_unlikely(!driver)) {
		SPDK_ERRLOG("Failed to alloc SCST target driver\n");
		return NULL;
	}

	driver->name = strdup(driver_name);
	if (spdk_unlikely(!driver->name)) {
		SPDK_ERRLOG("Failed to alloc target driver name %s\n", driver_name);
		goto out_err;
	}

	driver->path = spdk_sprintf_alloc("%s/%s/%s", SCST_ROOT, SCST_TARGETS, driver->name);
	if (spdk_unlikely(!driver->path)) {
		SPDK_ERRLOG("Failed to alloc target driver path\n");
		goto out_err;
	}

	TAILQ_INIT(&driver->target_list);

	return driver;

out_err:
	scst_target_driver_free(driver);

	return NULL;
}

static void
scst_target_driver_free(struct scst_target_driver *driver)
{
	free((char *) driver->path);
	free((char *) driver->name);
	free(driver);
}

static void
scst_target_driver_destroy(struct scst_target_driver *driver)
{
	struct scst_target *target, *tmp;

	TAILQ_FOREACH_SAFE(target, &driver->target_list, list, tmp) {
		scst_target_destroy(target);
	}
}

static struct scst_target *
scst_target_driver_find(struct scst_target_driver *driver, const char *target_name)
{
	struct scst_target *target;

	TAILQ_FOREACH(target, &driver->target_list, list) {
		if (!strcmp(target_name, target->name)) {
			return target;
		}
	}

	return NULL;
}

struct scst_target_driver *
scst_target_driver_next(struct scst_target_driver *driver)
{
	struct scst *scst = scst_get_instance();

	return !driver ? TAILQ_FIRST(&scst->driver_list) : TAILQ_NEXT(driver, list);
}

static struct scst_target *
scst_target_alloc(struct scst_target_driver *driver, const char *name)
{
	struct scst_target *target;

	target = calloc(1, sizeof(*target));
	if (spdk_unlikely(!target)) {
		SPDK_ERRLOG("Failed to alloc SCST target\n");
		return NULL;
	}

	target->name = strdup(name);
	if (spdk_unlikely(!target->name)) {
		SPDK_ERRLOG("Failed to alloc SCST target name\n");
		goto out_err;
	}

	target->path = spdk_sprintf_alloc("%s/%s", driver->path, target->name);
	if (spdk_unlikely(!target->path)) {
		SPDK_ERRLOG("Failed to alloc SCST target path\n");
		goto out_err;
	}

	target->driver = driver;

	return target;

out_err:
	scst_target_free(target);

	return NULL;
}

static void
scst_target_free(struct scst_target *target)
{
	free((char *) target->path);
	free((char *) target->name);
	free(target);
}

static void
scst_target_destroy(struct scst_target *target)
{
	struct scst_target_driver *driver = target->driver;

	TAILQ_REMOVE(&driver->target_list, target, list);

	scst_put_target_driver(driver);

	scst_target_free(target);
}

struct scst_target *
scst_target_next(struct scst_target_driver *driver, struct scst_target *target)
{
	return !target ? TAILQ_FIRST(&driver->target_list) : TAILQ_NEXT(target, list);
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

	args->filepath = scst_target_driver_mgmt(driver_name);
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

	args->filepath = scst_target_driver_mgmt(driver_name);
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

	if (scst_find_target(params->driver_name, params->target_name)) {
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

	rc = scst_add_target(params->driver_name, params->target_name);

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
	scst_pipeline(&scst_target_add_properties, cb_fn, cb_arg, params);
}

static void
target_del_step(struct sto_pipeline *pipe)
{
	struct scst_target_params *params = sto_pipeline_get_priv(pipe);

	if (!scst_find_target(params->driver_name, params->target_name)) {
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

	rc = scst_remove_target(params->driver_name, params->target_name);
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
	scst_pipeline(&scst_target_del_properties, cb_fn, cb_arg, params);
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
	TAILQ_INIT(&scst->driver_list);

	return scst;

free_config_path:
	free((char *) scst->config_path);

free_scst:
	free(scst);

	return NULL;
}

static void
scst_destroy_handlers(struct scst *scst)
{
	struct scst_device_handler *handler, *tmp;

	TAILQ_FOREACH_SAFE(handler, &scst->handler_list, list, tmp) {
		scst_device_handler_destroy(handler);
	}
}

static void
scst_destroy_drivers(struct scst *scst)
{
	struct scst_target_driver *driver, *tmp;

	TAILQ_FOREACH_SAFE(driver, &scst->driver_list, list, tmp) {
		scst_target_driver_destroy(driver);
	}
}

static void
scst_destroy(struct scst *scst)
{
	scst_destroy_drivers(scst);
	scst_destroy_handlers(scst);

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
	struct scst *scst = scst_get_instance();
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
	struct scst *scst = scst_get_instance();
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
	struct scst *scst = scst_get_instance();
	struct scst_device_handler *handler;

	handler = scst_find_device_handler(scst, handler_name);
	if (spdk_unlikely(!handler)) {
		return NULL;
	}

	return scst_device_handler_find(handler, device_name);
}

int
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

static struct scst_target_driver *
scst_find_target_driver(struct scst *scst, const char *driver_name)
{
	struct scst_target_driver *driver;

	TAILQ_FOREACH(driver, &scst->driver_list, list) {
		if (!strcmp(driver_name, driver->name)) {
			return driver;
		}
	}

	return NULL;
}

static struct scst_target_driver *
scst_get_target_driver(const char *driver_name)
{
	struct scst *scst = scst_get_instance();
	struct scst_target_driver *driver;

	driver = scst_find_target_driver(scst, driver_name);
	if (!driver) {
		driver = scst_target_driver_alloc(driver_name);
		if (spdk_unlikely(!driver)) {
			SPDK_ERRLOG("Failed to alloc %s driver\n",
				    driver_name);
			return NULL;
		}

		TAILQ_INSERT_TAIL(&scst->driver_list, driver, list);
	}

	driver->ref_cnt++;

	return driver;
}

static void
scst_put_target_driver(struct scst_target_driver *driver)
{
	struct scst *scst = scst_get_instance();
	int ref_cnt = --driver->ref_cnt;

	assert(ref_cnt >= 0);

	if (ref_cnt == 0) {
		TAILQ_REMOVE(&scst->driver_list, driver, list);
		scst_target_driver_free(driver);
	}
}

static struct scst_target *
scst_find_target(const char *driver_name, const char *target_name)
{
	struct scst *scst = scst_get_instance();
	struct scst_target_driver *driver;

	driver = scst_find_target_driver(scst, driver_name);
	if (spdk_unlikely(!driver)) {
		return NULL;
	}

	return scst_target_driver_find(driver, target_name);
}

int
scst_add_target(const char *driver_name, const char *target_name)
{
	struct scst_target_driver *driver;
	struct scst_target *target;

	if (scst_find_target(driver_name, target_name)) {
		SPDK_ERRLOG("SCST target %s is already exist\n", target_name);
		return -EEXIST;
	}

	driver = scst_get_target_driver(driver_name);
	if (spdk_unlikely(!driver)) {
		SPDK_ERRLOG("Failed to get %s driver\n", driver_name);
		return -ENOMEM;
	}

	target = scst_target_alloc(driver, target_name);
	if (spdk_unlikely(!target)) {
		SPDK_ERRLOG("Failed to alloc %s SCST target\n", target_name);
		goto put_handler;
	}

	TAILQ_INSERT_TAIL(&driver->target_list, target, list);

	SPDK_ERRLOG("SCST target %s driver [%s] was added\n",
		    target->name, driver->name);

	return 0;

put_handler:
	scst_put_target_driver(driver);

	return -ENOMEM;
}

static int
scst_remove_target(const char *driver_name, const char *target_name)
{
	struct scst_target *target;

	target = scst_find_target(driver_name, target_name);
	if (spdk_unlikely(!target)) {
		SPDK_ERRLOG("Failed to find `%s` SCST target to remove\n",
			    target_name);
		return -ENOENT;
	}

	scst_target_destroy(target);

	return 0;
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

void
scst_pipeline(const struct sto_pipeline_properties *properties,
	      sto_generic_cb cb_fn, void *cb_arg, void *priv)
{
	struct scst *scst = g_scst;

	sto_pipeline_alloc_and_run(scst->engine, properties, cb_fn, cb_arg, priv);
}

struct scst *
scst_get_instance(void)
{
	return g_scst;
}
