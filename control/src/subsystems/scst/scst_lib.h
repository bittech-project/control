#ifndef _SCST_LIB_H
#define _SCST_LIB_H

#include <spdk/util.h>

#include "scst.h"
#include "sto_readdir_front.h"

/* ./scst/src/scst.ko */
/* - ./fcst/fcst.ko */

/* - ./iscsi-scst/kernel/iscsi-scst.ko */
/* - - ./iscsi-scst/kernel/isert-scst/isert-scst.ko */

/* - ./qla2x00t-32gbit/qla2xxx_scst.ko */
/* - - ./qla2x00t-32gbit/qla2x00-target/qla2x00tgt.ko */

/* - ./qla2x00t/qla2xxx_scst.ko */
/* - ./qla2x00t/qla2x00-target/qla2x00tgt.ko */

/* - ./scst/src/dev_handlers/scst_tape.ko */
/* - ./scst/src/dev_handlers/scst_cdrom.ko */
/* - ./scst/src/dev_handlers/scst_changer.ko */
/* - ./scst/src/dev_handlers/scst_disk.ko */
/* - ./scst/src/dev_handlers/scst_modisk.ko */
/* - ./scst/src/dev_handlers/scst_processor.ko */
/* - ./scst/src/dev_handlers/scst_raid.ko */
/* - ./scst/src/dev_handlers/scst_user.ko */
/* - ./scst/src/dev_handlers/scst_vdisk.ko */

/* - ./scst_local/scst_local.ko */

/* - ./srpt/src/ib_srpt.ko */

struct scst_write_file_req {
	struct scst_req req;

	const char *file;
	char *data;
};
SCST_REQ_DEFINE(write_file)

struct scst_readdir_req {
	struct scst_req req;

	const char *name;
	char *dirpath;

	const char *exclude_str;

	struct sto_dirents dirents;
};
SCST_REQ_DEFINE(readdir)

struct scst_driver_init_req {
	struct scst_req req;

	TAILQ_HEAD(, scst_driver) drivers;
	struct scst_driver *drv;
};

struct scst_driver_deinit_req {
	struct scst_req req;

	TAILQ_HEAD(, scst_driver) drivers;
	struct scst_driver *drv;
};

SCST_REQ_DEFINE(driver_init)
SCST_REQ_DEFINE(driver_deinit)

int scst_driver_init_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_driver_deinit_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_handler_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dev_open_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dev_close_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dev_resync_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dev_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dgrp_add_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dgrp_del_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dgrp_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dgrp_add_dev_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_dgrp_del_dev_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_target_add_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_target_del_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);
int scst_target_list_decode_cdb(struct scst_req *req, const struct spdk_json_val *cdb);

#endif /* _SCST_LIB_H */
