#include <spdk/stdinc.h>
#include <spdk/likely.h>
#include <spdk/log.h>
#include <spdk/string.h>
#include <spdk/json.h>

#include "scst_lib.h"

#include "sto_async.h"
#include "sto_rpc_aio.h"
#include "sto_tree.h"
#include "sto_inode.h"
#include "sto_json.h"
#include "sto_hash.h"

struct spdk_json_write_ctx;

#define SCST_DEF_CONFIG_PATH "/etc/control.scst.json"

static void scst_device_handler_free(struct scst_device_handler *handler);

static void scst_device_free(struct scst_device *device);
static void scst_device_destroy(struct scst_device *device);

static void scst_target_driver_free(struct scst_target_driver *driver);

static void scst_target_free(struct scst_target *target);
static void scst_target_destroy(struct scst_target *target);

static struct scst_ini_group *scst_ini_group_alloc(struct scst_target *target, const char *name);
static void scst_ini_group_free(struct scst_ini_group *ini_group);
static void scst_ini_group_destroy(struct scst_ini_group *ini_group);

static struct scst_lun *scst_lun_alloc(struct scst_device *device, uint32_t lun_id);
static void scst_lun_free(struct scst_lun *lun);
static struct scst_lun *scst_lun_list_find(struct scst_lun_list *lun_list, uint32_t lun_id);
static int scst_lun_list_add(struct scst_lun_list *lun_list, struct scst_device *device, uint32_t lun_id);
static int scst_lun_list_remove(struct scst_lun_list *lun_list, uint32_t lun_id);

static void scst_put_device_handler(struct scst_device_handler *handler);

static void scst_put_target_driver(struct scst_target_driver *driver);


static struct scst_device_handler *
scst_device_handler_alloc(struct scst *scst, const char *handler_name)
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

	handler->scst = scst;

	TAILQ_INIT(&handler->device_list);

	return handler;

out_err:
	scst_device_handler_free(handler);

	return NULL;
}

static void
scst_device_handler_free(struct scst_device_handler *handler)
{
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

struct scst_device_handler *
scst_device_handler_next(struct scst *scst, struct scst_device_handler *handler)
{
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

	device->handler = handler;

	sto_hash_elem_init(&device->he, device->name, strlen(device->name));

	return device;

out_err:
	scst_device_free(device);

	return NULL;
}

static void
scst_device_free(struct scst_device *device)
{
	free((char *) device->name);
	free(device);
}

static void
scst_device_destroy(struct scst_device *device)
{
	struct scst_device_handler *handler = device->handler;

	TAILQ_REMOVE(&handler->device_list, device, list);

	sto_hash_elem_del(&device->he);

	scst_put_device_handler(handler);

	scst_device_free(device);
}

struct scst_device *
scst_device_next(struct scst_device_handler *handler, struct scst_device *device)
{
	return !device ? TAILQ_FIRST(&handler->device_list) : TAILQ_NEXT(device, list);
}

static struct scst_target_driver *
scst_target_driver_alloc(struct scst *scst, const char *driver_name)
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

	driver->scst = scst;

	TAILQ_INIT(&driver->target_list);

	return driver;

out_err:
	scst_target_driver_free(driver);

	return NULL;
}

static void
scst_target_driver_free(struct scst_target_driver *driver)
{
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
scst_target_driver_next(struct scst *scst, struct scst_target_driver *driver)
{
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

	target->driver = driver;

	TAILQ_INIT(&target->lun_list);
	TAILQ_INIT(&target->group_list);

	return target;

out_err:
	scst_target_free(target);

	return NULL;
}

static void
scst_target_free(struct scst_target *target)
{
	free((char *) target->name);
	free(target);
}

static void
scst_target_destroy(struct scst_target *target)
{
	struct scst_target_driver *driver = target->driver;
	struct scst_ini_group *ini_group, *tmp;

	TAILQ_REMOVE(&driver->target_list, target, list);

	scst_put_target_driver(driver);

	TAILQ_FOREACH_SAFE(ini_group, &target->group_list, list, tmp) {
		scst_ini_group_destroy(ini_group);
	}

	scst_target_free(target);
}

struct scst_target *
scst_target_next(struct scst_target_driver *driver, struct scst_target *target)
{
	return !target ? TAILQ_FIRST(&driver->target_list) : TAILQ_NEXT(target, list);
}

static struct scst_ini_group *
scst_target_find_ini_group(struct scst_target *target, const char *ini_group_name)
{
	struct scst_ini_group *ini_group;

	TAILQ_FOREACH(ini_group, &target->group_list, list) {
		if (!strcmp(ini_group_name, ini_group->name)) {
			return ini_group;
		}
	}

	return NULL;
}

static int
scst_target_add_ini_group(struct scst_target *target, const char *ini_group_name)
{
	struct scst_ini_group *ini_group;

	if (scst_target_find_ini_group(target, ini_group_name)) {
		SPDK_ERRLOG("ini group `%s` has arleady been in target `%s`\n",
			    ini_group_name, target->name);
		return -EEXIST;
	}

	ini_group = scst_ini_group_alloc(target, ini_group_name);
	if (spdk_unlikely(!ini_group)) {
		SPDK_ERRLOG("Failed to alloc ini group `%s`\n",
			    ini_group_name);
		return -ENOMEM;
	}

	TAILQ_INSERT_TAIL(&target->group_list, ini_group, list);

	SPDK_ERRLOG("SCST ini_group %s target [%s] was added\n",
		    ini_group->name, target->name);

	return 0;
}

static int
scst_target_remove_ini_group(struct scst_target *target, const char *ini_group_name)
{
	struct scst_ini_group *ini_group;

	ini_group = scst_target_find_ini_group(target, ini_group_name);
	if (spdk_unlikely(!ini_group)) {
		SPDK_ERRLOG("ini group `%s` has not been found in target `%s`\n",
			    ini_group_name, target->name);
		return -ENOENT;
	}

	scst_ini_group_destroy(ini_group);

	return 0;
}

static inline struct scst_lun *
scst_target_find_lun(struct scst_target *target, uint32_t lun_id)
{
	return scst_lun_list_find(&target->lun_list, lun_id);
}

static inline int
scst_target_add_lun(struct scst_target *target, struct scst_device *device, uint32_t lun_id)
{
	return scst_lun_list_add(&target->lun_list, device, lun_id);
}

static inline int
scst_target_remove_lun(struct scst_target *target, uint32_t lun_id)
{
	return scst_lun_list_remove(&target->lun_list, lun_id);
}

static struct scst_ini_group *
scst_ini_group_alloc(struct scst_target *target, const char *name)
{
	struct scst_ini_group *ini_group;

	ini_group = calloc(1, sizeof(*ini_group));
	if (spdk_unlikely(!ini_group)) {
		SPDK_ERRLOG("Failed to alloc SCST ini group\n");
		return NULL;
	}

	ini_group->name = strdup(name);
	if (spdk_unlikely(!ini_group->name)) {
		SPDK_ERRLOG("Failed to alloc SCST ini group name\n");
		goto out_err;
	}

	ini_group->target = target;

	TAILQ_INIT(&ini_group->lun_list);

	return ini_group;

out_err:
	scst_ini_group_free(ini_group);

	return NULL;
}

static void
scst_ini_group_free(struct scst_ini_group *ini_group)
{
	free((char *) ini_group->name);
	free(ini_group);
}

static void
scst_ini_group_destroy(struct scst_ini_group *ini_group)
{
	struct scst_target *target = ini_group->target;

	TAILQ_REMOVE(&target->group_list, ini_group, list);

	scst_ini_group_free(ini_group);
}

struct scst_ini_group *
scst_ini_group_next(struct scst_target *target, struct scst_ini_group *ini_group)
{
	return !ini_group ? TAILQ_FIRST(&target->group_list) : TAILQ_NEXT(ini_group, list);
}

static inline struct scst_lun *
scst_ini_group_find_lun(struct scst_ini_group *group, uint32_t lun_id)
{
	return scst_lun_list_find(&group->lun_list, lun_id);
}

static inline int
scst_ini_group_add_lun(struct scst_ini_group *group, struct scst_device *device, uint32_t lun_id)
{
	return scst_lun_list_add(&group->lun_list, device, lun_id);
}

static inline int
scst_ini_group_remove_lun(struct scst_ini_group *group, uint32_t lun_id)
{
	return scst_lun_list_remove(&group->lun_list, lun_id);
}

static struct scst_lun *
scst_lun_alloc(struct scst_device *device, uint32_t lun_id)
{
	struct scst_lun *lun;

	lun = calloc(1, sizeof(*lun));
	if (spdk_unlikely(!lun)) {
		SPDK_ERRLOG("Failed to alloc SCST %u lun for device %s\n",
			    lun_id, device->name);
		return NULL;
	}

	lun->device = device;
	lun->id = lun_id;

	return lun;
}

static void
scst_lun_free(struct scst_lun *lun)
{
	free(lun);
}

static struct scst_lun *
scst_lun_list_find(struct scst_lun_list *lun_list, uint32_t lun_id)
{
	struct scst_lun *lun;

	TAILQ_FOREACH(lun, lun_list, list) {
		if (lun_id == lun->id) {
			return lun;
		}
	}

	return NULL;
}

static int
scst_lun_list_add(struct scst_lun_list *lun_list, struct scst_device *device, uint32_t lun_id)
{
	struct scst_lun *lun;

	if (scst_lun_list_find(lun_list, lun_id)) {
		SPDK_ERRLOG("lun `%u` has arleady presented in SCST\n", lun_id);
		return -EEXIST;
	}

	lun = scst_lun_alloc(device, lun_id);
	if (spdk_unlikely(!lun)) {
		SPDK_ERRLOG("Failed to alloc lun `%u`\n", lun_id);
		return -ENOMEM;
	}

	TAILQ_INSERT_TAIL(lun_list, lun, list);

	SPDK_ERRLOG("LUN %u device[%s] was added\n", lun_id, device->name);

	return 0;
}

static int
scst_lun_list_remove(struct scst_lun_list *lun_list, uint32_t lun_id)
{
	struct scst_lun *lun;

	lun = scst_lun_list_find(lun_list, lun_id);
	if (spdk_unlikely(!lun)) {
		SPDK_ERRLOG("lun `%u` has not been presented in SCST\n", lun_id);
		return -ENOENT;
	}

	TAILQ_REMOVE(lun_list, lun, list);

	scst_lun_free(lun);

	SPDK_ERRLOG("LUN %u was removed\n", lun_id);

	return 0;
}

struct scst *
scst_create(void)
{
	struct scst *scst;
	int rc;

	scst = calloc(1, sizeof(*scst));
	if (spdk_unlikely(!scst)) {
		SPDK_ERRLOG("Failed to alloc SCST instance\n");
		return NULL;
	}

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

#define SCST_DEVICE_LOOKUP_MAP_SIZE 64
	rc = sto_hash_init(&scst->device_lookup_map, SCST_DEVICE_LOOKUP_MAP_SIZE);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to initialize SCST device lookup map\n");
		goto destroy_engine;
	}

	TAILQ_INIT(&scst->handler_list);
	TAILQ_INIT(&scst->driver_list);

	return scst;

destroy_engine:
	sto_pipeline_engine_destroy(scst->engine);

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

void
scst_destroy(struct scst *scst)
{
	scst_destroy_drivers(scst);
	scst_destroy_handlers(scst);

	sto_hash_destroy(&scst->device_lookup_map);
	sto_pipeline_engine_destroy(scst->engine);
	free((char *) scst->config_path);
	free(scst);
}

void
scst_pipeline(struct scst *scst, const struct sto_pipeline_properties *properties,
	      sto_generic_cb cb_fn, void *cb_arg, void *priv)
{
	sto_pipeline_alloc_and_run(scst->engine, properties, cb_fn, cb_arg, priv);
}

static char *
scst_available_attrs_line(char **lines, const char *prefix)
{
	int i, prefix_len;

	prefix_len = strlen(prefix);

	for (i = 0; lines[i] != NULL; i++) {
		if (!strncmp(prefix, lines[i], prefix_len)) {
			return lines[i] + prefix_len + 1;
		}
	}

	return NULL;
}

static void
scst_available_attrs(char **available_attrs)
{
	int i;

	for (i = 0; available_attrs[i] != NULL; i++) {
		char *attr = available_attrs[i];
		int attr_len = strlen(attr);

		if (attr[attr_len - 1] == ',') {
			attr[attr_len - 1] = '\0';
		}
	}

	return;
}

static char **
scst_available_attrs_create(const char *buf, const char *prefix)
{
	char **lines;
	char **available_attrs = NULL, *available_attrs_line;

	if (spdk_unlikely(!prefix || !buf)) {
		SPDK_ERRLOG("Buf or Prefix is NULL!\n");
		return NULL;
	}

	lines = spdk_strarray_from_string(buf, "\n");
	if (spdk_unlikely(!lines)) {
		SPDK_ERRLOG("Failed to split scst attr filter\n");
		return NULL;
	}

	available_attrs_line = scst_available_attrs_line(lines, prefix);
	if (!available_attrs_line) {
		SPDK_ERRLOG("Failed to find available attrs line\n");
		goto out;
	}

	available_attrs = spdk_strarray_from_string(available_attrs_line, " ");
	if (spdk_unlikely(!available_attrs)) {
		SPDK_ERRLOG("Failed to split scst attr line\n");
		goto out;
	}

	scst_available_attrs(available_attrs);

out:
	spdk_strarray_free(lines);

	return available_attrs;
}

struct read_available_attrs_ctx {
	const char *prefix;

	void *cb_arg;
	sto_generic_cb cb_fn;

	char ***available_attrs;
};

static void
read_available_attrs_done(void *priv, char *buf, int rc)
{
	struct read_available_attrs_ctx *ctx = priv;

	if (spdk_unlikely(rc)) {
		goto out;
	}

	*ctx->available_attrs = scst_available_attrs_create(buf, ctx->prefix);
	if (spdk_unlikely(!*ctx->available_attrs)) {
		rc = -ENOMEM;
		goto out;
	}

	scst_available_attrs_print(*ctx->available_attrs);

out:
	ctx->cb_fn(ctx->cb_arg, rc);
	free(ctx);

	free(buf);
}

void
scst_read_available_attrs(const char *mgmt_path, const char *prefix,
			  sto_generic_cb cb_fn, void *cb_arg, char ***available_attrs)
{
	struct read_available_attrs_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc config read available_attrs ctx\n");
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	ctx->cb_fn = cb_fn;
	ctx->cb_arg = cb_arg;
	ctx->prefix = prefix;
	ctx->available_attrs = available_attrs;

	sto_rpc_readfile(mgmt_path, 0, read_available_attrs_done, ctx);
}

void
scst_available_attrs_print(char **available_attrs)
{
	int i;

	if (!available_attrs) {
		return;
	}

	SPDK_ERRLOG("GLEB: Print available attrs:");

	for (i = 0; available_attrs[i] != NULL; i++) {
		printf(" %s,", available_attrs[i]);
	}

	printf("\n");
}

bool
scst_available_attrs_find(char **available_attrs, char *attr)
{
	int i;

	for (i = 0; available_attrs[i] != NULL; i++) {
		if (!strcmp(available_attrs[i], attr)) {
			return true;
		}
	}

	return false;
}

void
scst_available_attrs_destroy(char **available_attrs)
{
	spdk_strarray_free(available_attrs);
}

static char *
scst_attr(const char *buf, bool nonkey)
{
	char *attr = NULL;
	char **lines;

	if (spdk_unlikely(!buf)) {
		return NULL;
	}

	lines = spdk_strarray_from_string(buf, "\n");
	if (spdk_unlikely(!lines)) {
		SPDK_ERRLOG("Failed to split scst attr\n");
		return NULL;
	}

	if (!nonkey && (!lines[1] || strcmp(lines[1], "[key]"))) {
		goto out;
	}

	attr = strdup(lines[0]);

out:
	spdk_strarray_free(lines);

	return attr;
}

static void
scst_serialize_attr(struct sto_inode *attr_inode, struct spdk_json_write_ctx *w)
{
	char *attr;

	if (sto_inode_read_only(attr_inode)) {
		return;
	}

	attr = scst_attr(sto_file_inode_buf(attr_inode), false);
	if (!attr) {
		return;
	}

	spdk_json_write_named_string(w, attr_inode->name, attr);

	free(attr);
}

void
scst_serialize_attrs(struct sto_tree_node *obj_node, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *attr_node;

	STO_TREE_FOREACH_TYPE(attr_node, obj_node, STO_INODE_TYPE_FILE) {
		struct sto_inode *inode = attr_node->inode;

		scst_serialize_attr(inode, w);
	}
}

static int
scst_write_attrs_cb(void *cb_ctx, struct spdk_json_write_ctx *w)
{
	struct sto_tree_node *attrs_node = cb_ctx;

	spdk_json_write_object_begin(w);

	scst_serialize_attrs(attrs_node, w);

	spdk_json_write_object_end(w);

	return 0;
}

struct read_attrs_ctx {
	struct sto_json_ctx json;

	scst_read_attrs_done_t cb_fn;
	void *cb_arg;
};

static void
read_attrs_done(void *cb_arg, struct sto_tree_node *tree_root, int rc)
{
	struct read_attrs_ctx *ctx = cb_arg;

	if (spdk_unlikely(rc)) {
		goto out;
	}

	rc = sto_json_ctx_write(&ctx->json, true, scst_write_attrs_cb, (void *) tree_root);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to dump SCST dev attributes\n");
		goto out;
	}

	sto_json_print("Print SCST attributes", ctx->json.values);

out:
	ctx->cb_fn(ctx->cb_arg, &ctx->json, rc);

	sto_json_ctx_destroy(&ctx->json);
	free(ctx);

	sto_tree_free(tree_root);
}

void
scst_read_attrs(const char *dirpath, scst_read_attrs_done_t cb_fn, void *cb_arg)
{
	struct read_attrs_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc dev read attrs ctx\n");
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	ctx->cb_fn = cb_fn;
	ctx->cb_arg = cb_arg;

	sto_tree(dirpath, 1, false, read_attrs_done, ctx);
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
scst_get_device_handler(struct scst *scst, const char *handler_name)
{
	struct scst_device_handler *handler;

	handler = scst_find_device_handler(scst, handler_name);
	if (!handler) {
		handler = scst_device_handler_alloc(scst, handler_name);
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
	struct scst *scst = handler->scst;
	int ref_cnt = --handler->ref_cnt;

	assert(ref_cnt >= 0);

	if (ref_cnt == 0) {
		TAILQ_REMOVE(&scst->handler_list, handler, list);
		scst_device_handler_free(handler);
	}
}

struct scst_device *
scst_find_device(struct scst *scst, const char *device_name)
{
	struct sto_hash_elem *he;

	he = sto_hash_lookup(&scst->device_lookup_map, device_name, strlen(device_name));
	if (!he) {
		return NULL;
	}

	return SPDK_CONTAINEROF(he, struct scst_device, he);
}

int
scst_add_device(struct scst *scst, const char *handler_name, const char *device_name)
{
	struct scst_device_handler *handler;
	struct scst_device *device;

	if (scst_find_device(scst, device_name)) {
		SPDK_ERRLOG("SCST device %s is already exist\n", device_name);
		return -EEXIST;
	}

	handler = scst_get_device_handler(scst, handler_name);
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

	sto_hash_add(&scst->device_lookup_map, &device->he);

	SPDK_ERRLOG("SCST device %s handler [%s] was added\n",
		    device->name, handler->name);

	return 0;

put_handler:
	scst_put_device_handler(handler);

	return -ENOMEM;
}

int
scst_remove_device(struct scst *scst, const char *device_name)
{
	struct scst_device *device;

	device = scst_find_device(scst, device_name);
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
scst_get_target_driver(struct scst *scst, const char *driver_name)
{
	struct scst_target_driver *driver;

	driver = scst_find_target_driver(scst, driver_name);
	if (!driver) {
		driver = scst_target_driver_alloc(scst, driver_name);
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
	struct scst *scst = driver->scst;
	int ref_cnt = --driver->ref_cnt;

	assert(ref_cnt >= 0);

	if (ref_cnt == 0) {
		TAILQ_REMOVE(&scst->driver_list, driver, list);
		scst_target_driver_free(driver);
	}
}

struct scst_target *
scst_find_target(struct scst *scst, const char *driver_name, const char *target_name)
{
	struct scst_target_driver *driver;

	driver = scst_find_target_driver(scst, driver_name);
	if (spdk_unlikely(!driver)) {
		return NULL;
	}

	return scst_target_driver_find(driver, target_name);
}

int
scst_add_target(struct scst *scst, const char *driver_name, const char *target_name)
{
	struct scst_target_driver *driver;
	struct scst_target *target;

	if (scst_find_target(scst, driver_name, target_name)) {
		SPDK_ERRLOG("SCST target %s is already exist\n", target_name);
		return -EEXIST;
	}

	driver = scst_get_target_driver(scst, driver_name);
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

int
scst_remove_target(struct scst *scst, const char *driver_name, const char *target_name)
{
	struct scst_target *target;

	target = scst_find_target(scst, driver_name, target_name);
	if (spdk_unlikely(!target)) {
		SPDK_ERRLOG("Failed to find `%s` SCST target to remove\n",
			    target_name);
		return -ENOENT;
	}

	scst_target_destroy(target);

	return 0;
}

struct scst_ini_group *
scst_find_ini_group(struct scst *scst, const char *driver_name,
		    const char *target_name, const char *ini_group_name)
{
	struct scst_target *target;

	target = scst_find_target(scst, driver_name, target_name);
	if (spdk_unlikely(!target)) {
		return NULL;
	}

	return scst_target_find_ini_group(target, ini_group_name);
}

int
scst_add_ini_group(struct scst *scst, const char *driver_name,
		   const char *target_name, const char *ini_group_name)
{
	struct scst_target *target;

	target = scst_find_target(scst, driver_name, target_name);
	if (spdk_unlikely(!target)) {
		SPDK_ERRLOG("Cann't find SCST target %s\n", target_name);
		return -ENOENT;
	}

	return scst_target_add_ini_group(target, ini_group_name);
}

int
scst_remove_ini_group(struct scst *scst, const char *driver_name,
		      const char *target_name, const char *ini_group_name)
{
	struct scst_target *target;

	target = scst_find_target(scst, driver_name, target_name);
	if (spdk_unlikely(!target)) {
		SPDK_ERRLOG("Failed to find `%s` SCST target to remove ini group %s\n",
			    target_name, ini_group_name);
		return -ENOENT;
	}

	return scst_target_remove_ini_group(target, ini_group_name);
}

struct scst_lun *
scst_find_lun(struct scst *scst, const char *driver_name,
	      const char *target_name, const char *ini_group_name,
	      uint32_t lun_id)
{
	if (ini_group_name) {
		struct scst_ini_group *group;

		group = scst_find_ini_group(scst, driver_name, target_name, ini_group_name);
		if (spdk_unlikely(!group)) {
			return NULL;
		}

		return scst_ini_group_find_lun(group, lun_id);
	} else {
		struct scst_target *target;

		target = scst_find_target(scst, driver_name, target_name);
		if (spdk_unlikely(!target)) {
			return NULL;
		}

		return scst_target_find_lun(target, lun_id);
	}
}

int
scst_add_lun(struct scst *scst, const char *driver_name,
	     const char *target_name, const char *ini_group_name,
	     const char *device_name, uint32_t lun_id)
{
	struct scst_device *device;

	device = scst_find_device(scst, device_name);
	if (spdk_unlikely(!device)) {
		SPDK_ERRLOG("Cann't find SCST device %s\n", device_name);
		return -ENOENT;
	}

	if (ini_group_name) {
		struct scst_ini_group *group;

		group = scst_find_ini_group(scst, driver_name, target_name, ini_group_name);
		if (spdk_unlikely(!group)) {
			SPDK_ERRLOG("Cann't find SCST ini group %s\n", ini_group_name);
			return -ENOENT;
		}

		return scst_ini_group_add_lun(group, device, lun_id);
	} else {
		struct scst_target *target;

		target = scst_find_target(scst, driver_name, target_name);
		if (spdk_unlikely(!target)) {
			SPDK_ERRLOG("Cann't find SCST target %s\n", target_name);
			return -ENOENT;
		}

		return scst_target_add_lun(target, device, lun_id);
	}
}

int
scst_remove_lun(struct scst *scst, const char *driver_name,
		const char *target_name, const char *ini_group_name,
		uint32_t lun_id)
{
	if (ini_group_name) {
		struct scst_ini_group *group;

		group = scst_find_ini_group(scst, driver_name, target_name, ini_group_name);
		if (spdk_unlikely(!group)) {
			SPDK_ERRLOG("Cann't find SCST ini group %s\n", ini_group_name);
			return -ENOENT;
		}

		return scst_ini_group_remove_lun(group, lun_id);
	} else {
		struct scst_target *target;

		target = scst_find_target(scst, driver_name, target_name);
		if (spdk_unlikely(!target)) {
			SPDK_ERRLOG("Cann't find SCST target %s\n", target_name);
			return -ENOENT;
		}

		return scst_target_remove_lun(target, lun_id);
	}
}
