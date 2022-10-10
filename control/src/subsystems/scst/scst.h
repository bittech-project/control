#ifndef _SCST_H_
#define _SCST_H_

#include "sto_core.h"
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

enum scst_tgt_type {
	SCST_TGT_LOCAL,
	SCST_TGT_FCST,
	SCST_TGT_ISCSI,
	SCST_TGT_ISER,
	SCST_TGT_IB,
	SCST_TGT_QLA,
	SCST_TGT_COUNT,
};

enum scst_dh_type {
	SCST_DH_TAPE,
	SCST_DH_CDROM,
	SCST_DH_CHANGER,
	SCST_DH_DISK,
	SCST_DH_MODISK,
	SCST_DH_PROCESSOR,
	SCST_DH_RAID,
	SCST_DH_USER,
	SCST_DH_VDISK,
	SCST_DH_COUNT
};

enum scst_drv_type {
	SCST_DRV_CORE,
	SCST_DRV_LOCAL,
	SCST_DRV_FCST,
	SCST_DRV_ISCSI,
	SCST_DRV_ISER,
	SCST_DRV_IB,
	SCST_DRV_QLA,
	SCST_DRV_QLA_TARGET,
	SCST_DRV_TAPE,
	SCST_DRV_CDROM,
	SCST_DRV_CHANGER,
	SCST_DRV_DISK,
	SCST_DRV_MODISK,
	SCST_DRV_PROCESSOR,
	SCST_DRV_RAID,
	SCST_DRV_USER,
	SCST_DRV_VDISK,
	SCST_DRV_COUNT,
};

enum scst_drv_status {
	DRV_LOADED,
	DRV_NEED_LOAD,
	DRV_UNLOADED,
	DRV_NEED_UNLOAD,
};

struct scst_driver_dep {
	struct scst_driver *drv;
	TAILQ_ENTRY(scst_driver_dep) list;
};

struct scst_driver {
	const char *name;
	enum scst_drv_type type;

	enum scst_drv_status status;

	TAILQ_ENTRY(scst_driver) list;

	TAILQ_HEAD(, scst_driver_dep) master_list;
	TAILQ_HEAD(, scst_driver_dep) slave_list;
};

struct scst_tgt {
	const char *name;
	enum scst_tgt_type type;
};

struct scst_dh {
	const char *name;
	enum scst_dh_type type;
};

struct scst {
	struct scst_tgt tgts[SCST_TGT_COUNT];
	struct scst_dh dhs[SCST_DH_COUNT];

	struct scst_driver drivers[SCST_DRV_COUNT];

	bool initialized;
};

struct scst_req;
struct scst_cdbops;

typedef struct scst_req *(*scst_req_constructor_t)(const struct scst_cdbops *op);

struct scst_cdbops {
	struct sto_cdbops op;

	scst_req_constructor_t constructor;
};

typedef void (*scst_req_done_t)(void *priv);

typedef int (*scst_req_decode_cdb_t)(struct scst_req *req, const struct spdk_json_val *cdb);
typedef int (*scst_req_exec_t)(struct scst_req *req);
typedef void (*scst_req_free_t)(struct scst_req *req);

struct scst_req {
	struct scst *scst;

	void *priv;
	scst_req_done_t req_done;

	const struct scst_cdbops *op;

	scst_req_decode_cdb_t decode_cdb;
	scst_req_exec_t req_exec;
	scst_req_free_t req_free;
};

#define SCST_REQ_DEFINE(req_type)							\
static inline struct scst_ ## req_type ## _req *					\
to_ ## req_type ## _req(struct scst_req *req)						\
{											\
	return SPDK_CONTAINEROF(req, struct scst_ ## req_type ## _req, req);		\
}											\
											\
struct scst_req *scst_req_ ## req_type ## _constructor(const struct scst_cdbops *op);


#define SCST_REQ_REGISTER(req_type)							\
static void										\
scst_ ## req_type ## _req_free(struct scst_req *req)					\
{											\
	struct scst_ ## req_type ## _req * req_ ## req_type =				\
						to_ ## req_type ## _req(req);		\
	rte_free(req_ ## req_type);							\
}											\
											\
struct scst_req *									\
scst_req_ ## req_type ## _constructor(const struct scst_cdbops *op)			\
{											\
	struct scst_ ## req_type ## _req * req_ ## req_type;				\
	struct scst_req *req;								\
											\
	req_ ## req_type = rte_zmalloc(NULL, sizeof(*req_ ## req_type), 0);		\
	if (spdk_unlikely(!req_ ## req_type)) {						\
		SPDK_ERRLOG("Failed to alloc SCST req\n");				\
		return NULL;								\
	}										\
											\
	req = &req_ ## req_type->req;							\
											\
	scst_req_init(req, op);								\
											\
	req->decode_cdb = scst_req_ ## req_type ## _decode_cdb;				\
	req->req_exec = scst_ ## req_type;						\
	req->req_free = scst_ ## req_type ## _req_free;					\
											\
	return req;									\
}

void scst_subsystem_init(void);
void scst_subsystem_fini(void);

struct scst_tgt *scst_find_tgt_by_name(struct scst *scst, const char *name);
struct scst_dh *scst_find_dh_by_name(struct scst *scst, const char *name);
struct scst_driver *scst_find_drv_by_name(struct scst *scst, const char *name);

void scst_req_init(struct scst_req *req, const struct scst_cdbops *op);
int scst_req_submit(struct scst_req *req);

int scst_req_subprocess(const char *cmd[], int numargs,
			subprocess_done_t cmd_done, struct scst_req *req);

#endif /* _SCST_H_ */
