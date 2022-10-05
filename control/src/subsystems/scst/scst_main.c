#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_malloc.h>

#include "scst.h"
#include "sto_subprocess_front.h"

#define SCST_ROOT "/sys/kernel/scst_tgt"

/* Root-level */
#define SCST_SGV	"sgv"
#define SCST_HANDLERS	"handlers"
#define SCST_DEVICES	"devices"
#define SCST_TARGETS	"targets"
#define SCST_DEV_GROUPS	"device_groups"
#define SCST_QUEUE_RES	"last_sysfs_mgmt_res"

/* Device group specific */
#define SCST_DG_DEVICES	"devices"
#define SCST_DG_TGROUPS	"target_groups"

/* Target specific */
#define SCST_GROUPS	"ini_groups"
#define SCST_INITIATORS	"initiators"
#define SCST_SESSIONS	"sessions"
#define SCST_LUNS	"luns"

/* Files */
#define SCST_MGMT_IO	"mgmt"
#define SCST_VERSION_IO	"version"
#define SCST_TRACE_IO	"trace_level"
#define SCST_RESYNC_IO	"resync_size"
#define SCST_T10_IO	"t10_dev_id"

static struct scst g_scst;

static const char *const scst_module_names[] = {
	[__SCST_CORE]		= "scst",
	[__SCST_LOCAL]		= "scst_local",
	[__SCST_FCST]		= "fcst",
	[__SCST_ISCSI]		= "iscsi-scst",
	[__SCST_ISER]		= "isert-scst",
	[__SCST_IB]		= "ib_srpt",
	[__SCST_QLA]		= "qla2xxx_scst",
	[__SCST_QLA_TARGET]	= "qla2x00tgt"
};

const char *
scst_module_name(enum scst_module_bits idx)
{
	size_t index = idx;

	if (spdk_unlikely(index >= SPDK_COUNTOF(scst_module_names))) {
		assert(0);
	}

	return scst_module_names[index];
}

static const char *const scst_op_names[] = {
	[SCST_CONSTRUCT]	= "construct",
	[SCST_DESTRUCT]		= "destruct",
};

const char *
scst_op_name(enum scst_ops op)
{
	size_t index = op;

	if (spdk_unlikely(index >= SPDK_COUNTOF(scst_op_names))) {
		assert(0);
	}

	return scst_op_names[index];
}

static int
scst_req_subprocess(struct scst_req *req, const char *cmd[],
		    int numargs, subprocess_done_t cmd_done)
{
	struct sto_subprocess *subp;
	int rc = 0;

	subp = sto_subprocess_alloc(cmd, numargs, false);
	if (spdk_unlikely(!subp)) {
		SPDK_ERRLOG("Failed to create subprocess\n");
		return -ENOMEM;
	}

	sto_subprocess_init_cb(subp, cmd_done, req);

	rc = sto_subprocess_run(subp);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run subprocess\n");
		goto free_subp;
	}

	return 0;

free_subp:
	sto_subprocess_free(subp);

	return rc;
}

static void
scst_module_load_done(struct sto_subprocess *subp)
{
	struct scst_req *req = subp->priv;
	struct scst_construct_req *constr_req = to_construct_req(req);
	struct scst *scst = req->scst;
	int rc;

	rc = subp->returncode;
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Subprocess failed exec, rc=%d\n", rc);
		sto_subprocess_free(subp);
		req->req_done(req);
		return;
	}

	scst->load_map[constr_req->module_idx] = SCST_LOADED;

	sto_subprocess_free(subp);

	scst_req_submit(req);
}

static void
scst_module_unload_done(struct sto_subprocess *subp)
{
	struct scst_req *req = subp->priv;
	struct scst_destruct_req *destr_req = to_destruct_req(req);
	struct scst *scst = req->scst;

	scst->load_map[destr_req->module_idx] = SCST_NOT_LOADED;

	sto_subprocess_free(subp);

	scst_req_submit(req);
}

static int
scst_module_load(struct scst_req *req)
{
	struct scst_construct_req *constr_req = to_construct_req(req);
	const char *modprobe[] = {"modprobe", scst_module_name(constr_req->module_idx)};

	return scst_req_subprocess(req, modprobe, SPDK_COUNTOF(modprobe), scst_module_load_done);
}

static int
scst_module_unload(struct scst_req *req)
{
	struct scst_destruct_req *destr_req = to_destruct_req(req);
	const char *rmmod[] = {"rmmod", scst_module_name(destr_req->module_idx)};

	return scst_req_subprocess(req, rmmod, SPDK_COUNTOF(rmmod), scst_module_unload_done);
}

static void
scst_tag_modules(struct scst *scst, struct scst_construct_req *req)
{
	int i;

	if (scst->load_map[SCST_CORE] != SCST_LOADED) {
		req->modules_bitmap |= SCST_CORE;
	}

	if (req->modules_bitmap & SCST_ISER) {
		req->modules_bitmap |= SCST_ISCSI;
	}

	if (req->modules_bitmap & SCST_QLA_TARGET) {
		req->modules_bitmap |= SCST_QLA;
	}

	for (i = 0; i < __SCST_NR_BITS; i++) {
		if (scst_module_test_bit(req->modules_bitmap, i) &&
		    scst->load_map[i] != SCST_LOADED) {
			scst->load_map[i] = SCST_NEED_LOAD;
		}
	}
}

static int
scst_constructor(struct scst_req *req)
{
	struct scst_construct_req *constr_req = to_construct_req(req);
	struct scst *scst = req->scst;
	int i;

	if (!constr_req->is_tagged) {
		scst_tag_modules(scst, constr_req);
		constr_req->is_tagged = true;
	}

	for (i = 0; i < __SCST_NR_BITS; i++) {
		if (scst->load_map[i] == SCST_NEED_LOAD) {
			constr_req->module_idx = i;
			return scst_module_load(req);
		}
	}

	req->req_done(req);

	return 0;
}

static int
scst_destructor(struct scst_req *req)
{
	struct scst_destruct_req *destr_req = to_destruct_req(req);
	struct scst *scst = req->scst;
	int i;

	for (i = __SCST_NR_BITS - 1; i >= 0; i--) {
		if (scst->load_map[i] == SCST_LOADED) {
			destr_req->module_idx = i;
			return scst_module_unload(req);
		}
	}

	req->req_done(req);

	return 0;
}

typedef int (*scst_op_fn_t)(struct scst_req *req);

static const scst_op_fn_t scst_req_ops[] = {
	[SCST_CONSTRUCT] = scst_constructor,
	[SCST_DESTRUCT]  = scst_destructor,
};

static void
scst_req_init(struct scst_req *req, enum scst_ops op)
{
	req->scst = &g_scst;
	req->op = op;
}

static void
scst_construct_req_free(struct scst_req *req)
{
	struct scst_construct_req *constr_req = to_construct_req(req);
	rte_free(constr_req);
}

struct scst_req *
scst_construct_req_alloc(unsigned long modules_bitmap)
{
	struct scst_construct_req *constr_req;
	struct scst_req *req;

	constr_req = rte_zmalloc(NULL, sizeof(*constr_req), 0);
	if (spdk_unlikely(!constr_req)) {
		SPDK_ERRLOG("Failed to alloc SCST construct req\n");
		return NULL;
	}

	req = &constr_req->req;

	scst_req_init(req, SCST_CONSTRUCT);
	req->req_free = scst_construct_req_free;

	constr_req->modules_bitmap = modules_bitmap;

	return req;
}

static void
scst_destruct_req_free(struct scst_req *req)
{
	struct scst_destruct_req *destr_req = to_destruct_req(req);
	rte_free(destr_req);
}

struct scst_req *
scst_destruct_req_alloc(void)
{
	struct scst_destruct_req *destr_req;
	struct scst_req *req;

	destr_req = rte_zmalloc(NULL, sizeof(*destr_req), 0);
	if (spdk_unlikely(!destr_req)) {
		SPDK_ERRLOG("Failed to alloc SCST destruct req\n");
		return NULL;
	}

	req = &destr_req->req;

	scst_req_init(req, SCST_DESTRUCT);
	req->req_free = scst_destruct_req_free;

	return req;
}

void
scst_req_free(struct scst_req *req)
{
	req->req_free(req);
}

void
scst_req_init_cb(struct scst_req *req, scst_req_done_t scst_req_done, void *priv)
{
	req->req_done = scst_req_done;
	req->priv = priv;
}

int
scst_req_submit(struct scst_req *req)
{
	scst_op_fn_t op;
	int rc;

	op = scst_req_ops[req->op];

	rc = op(req);

	return rc;
}
