#ifndef _SCST_LIB_H
#define _SCST_LIB_H

#include <spdk/util.h>

#include "scst.h"

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

struct scst_dev_open_req {
	struct scst_req req;

	char *mgmt_path;
	char *parsed_cmd;
};

struct scst_dev_close_req {
	struct scst_req req;

	char *mgmt_path;
	char *parsed_cmd;
};

struct scst_dev_resync_req {
	struct scst_req req;

	char *mgmt_path;
	char *parsed_cmd;
};

SCST_REQ_DEFINE(driver_init)
SCST_REQ_DEFINE(driver_deinit)
SCST_REQ_DEFINE(dev_open)
SCST_REQ_DEFINE(dev_close)
SCST_REQ_DEFINE(dev_resync)

#endif /* _SCST_LIB_H */
