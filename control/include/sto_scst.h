#ifndef _STO_SCST_H
#define _STO_SCST_H

#include <spdk/util.h>

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

enum scst_module_bits {
	__SCST_CORE,
	__SCST_LOCAL,
	__SCST_FCST,
	__SCST_ISCSI,
	__SCST_ISER,
	__SCST_IB,
	__SCST_QLA,
	__SCST_QLA_TARGET,
	__SCST_NR_BITS
};

#define BIT(nr) (1ULL << (nr))

enum scst_module {
	SCST_CORE	= BIT(__SCST_CORE),
	SCST_LOCAL	= BIT(__SCST_LOCAL),
	SCST_FCST	= BIT(__SCST_FCST),
	SCST_ISCSI	= BIT(__SCST_ISCSI),
	SCST_ISER	= BIT(__SCST_ISER),
	SCST_IB		= BIT(__SCST_IB),
	SCST_QLA	= BIT(__SCST_QLA),
	SCST_QLA_TARGET	= BIT(__SCST_QLA_TARGET),
};

static inline bool
scst_module_test_bit(unsigned long bitmap, enum scst_module_bits bit)
{
	return bitmap & BIT(bit);
}

enum scst_module_status {
	SCST_NOT_LOADED,
	SCST_NEED_LOAD,
	SCST_LOADED,
};

struct scst_req;
typedef void (*scst_req_done_t)(struct scst_req *req);
typedef void (*scst_req_free_t)(struct scst_req *req);

struct scst {
	uint8_t load_map[__SCST_NR_BITS];
};

enum scst_ops {
	SCST_CONSTRUCT,
	SCST_DESTRUCT,
};

struct scst_req {
	struct scst *scst;

	void *priv;
	scst_req_done_t req_done;

	enum scst_ops op;

	scst_req_free_t req_free;
};

struct scst_construct_req {
	struct scst_req req;

	bool is_tagged;
	unsigned long modules_bitmap;

	enum scst_module_bits module_idx;
};

struct scst_destruct_req {
	struct scst_req req;

	enum scst_module_bits module_idx;
};

static inline struct scst_construct_req *
to_construct_req(struct scst_req *req)
{
	return SPDK_CONTAINEROF(req, struct scst_construct_req, req);
}

static inline struct scst_destruct_req *
to_destruct_req(struct scst_req *req)
{
	return SPDK_CONTAINEROF(req, struct scst_destruct_req, req);
}

const char *scst_module_name(enum scst_module_bits module_bit);

struct scst_req *scst_construct_req_alloc(unsigned long modules_bitmap);
struct scst_req *scst_destruct_req_alloc(void);
void scst_req_free(struct scst_req *req);

void scst_req_init_cb(struct scst_req *req, scst_req_done_t scst_req_done, void *priv);

int scst_req_submit(struct scst_req *req);

#endif /* _STO_SCST_H */
