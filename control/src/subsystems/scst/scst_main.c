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

#define SCST_DEF_CONFIG_PATH "/etc/control.scst.json"

struct scst_device_handler;

struct scst_device {
	struct scst_device_handler *handler;

	const char *name;
	const char *path;

	TAILQ_ENTRY(scst_device) list;
};

static struct scst_device *scst_device_alloc(struct scst_device_handler *handler, const char *name);
static void scst_device_free(struct scst_device *device);
static struct scst_device *scst_device_create(const char *handler_name, const char *device_name);
static void scst_device_destroy(struct scst_device *device);

struct scst_device_handler {
	const char *name;
	const char *path;
	const char *mgmt_path;

	TAILQ_HEAD(, scst_device) device_list;
	int ref_cnt;

	TAILQ_ENTRY(scst_device_handler) list;
};

static struct scst_device_handler *scst_device_handler_alloc(const char *handler_name);
static void scst_device_handler_free(struct scst_device_handler *handler);
static struct scst_device *scst_device_handler_find(struct scst_device_handler *handler,
						    const char *device_name);

struct scst {
	const char *sys_path;
	const char *config_path;

	struct sto_pipeline_engine *engine;

	TAILQ_HEAD(, scst_device_handler) handler_list;
};

static struct scst *g_scst;


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
	struct scst_device *device;

	if (scst_find_device(handler_name, device_name)) {
		SPDK_ERRLOG("SCST device %s is already exist\n", device_name);
		return -EEXIST;
	}

	device = scst_device_create(handler_name, device_name);
	if (spdk_unlikely(!device)) {
		SPDK_ERRLOG("Failed to create `%s` SCST device\n", device_name);
		return -ENOMEM;
	}

	return 0;
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

static void
scst_device_handler_destroy(struct scst_device_handler *handler)
{
	struct scst_device *device, *tmp;

	TAILQ_FOREACH_SAFE(device, &handler->device_list, list, tmp) {
		scst_device_destroy(device);
	}
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

static struct scst_device *
scst_device_create(const char *handler_name, const char *device_name)
{
	struct scst_device_handler *handler;
	struct scst_device *device;

	handler = scst_get_device_handler(handler_name);
	if (spdk_unlikely(!handler)) {
		SPDK_ERRLOG("Failed to get %s handler\n", handler_name);
		return NULL;
	}

	device = scst_device_alloc(handler, device_name);
	if (spdk_unlikely(!device)) {
		SPDK_ERRLOG("Failed to alloc %s SCST device\n", device_name);
		goto put_handler;
	}

	TAILQ_INSERT_TAIL(&handler->device_list, device, list);

	return device;

put_handler:
	scst_put_device_handler(handler);

	return NULL;
}

static void
scst_device_destroy(struct scst_device *device)
{
	struct scst_device_handler *handler = device->handler;

	TAILQ_REMOVE(&handler->device_list, device, list);

	scst_put_device_handler(handler);

	scst_device_free(device);
}

void
scst_pipeline(const struct sto_pipeline_properties *properties,
	      sto_generic_cb cb_fn, void *cb_arg, void *priv)
{
	struct scst *scst = g_scst;

	sto_pipeline_alloc_and_run(scst->engine, properties, cb_fn, cb_arg, priv);
}

void
scst_init(sto_generic_cb cb_fn, void *cb_arg)
{
	struct scst *scst;
	int rc = 0;

	if (g_scst) {
		SPDK_ERRLOG("FAILED: SCST has already been initialized\n");
		rc = -EINVAL;
		goto out;
	}

	scst = scst_create();
	if (spdk_unlikely(!scst)) {
		SPDK_ERRLOG("Faild to create SCST\n");
		rc = -ENOMEM;
		goto out;
	}

	g_scst = scst;
out:
	cb_fn(cb_arg, rc);
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
