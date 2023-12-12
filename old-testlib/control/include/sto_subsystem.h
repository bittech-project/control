#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

#include <spdk/queue.h>

#include "sto_hash.h"

struct sto_op_table;

struct sto_subsystem_ops {
	void (*init)(void);
	void (*fini)(void);
};

struct sto_subsystem {
	const char *name;

	const struct sto_op_table *op_table;
	const struct sto_shash ops_map;

	struct sto_subsystem_ops *ops;

	TAILQ_ENTRY(sto_subsystem) list;
};

void sto_add_subsystem(struct sto_subsystem *subsystem);
struct sto_subsystem *sto_subsystem_find(const char *name);

void sto_subsystem_init_next(int rc);
void sto_subsystem_fini_next(void);

#define STO_SUBSYSTEM_REGISTER(SUBSYSTEM, OP_TABLE, OPS)				\
static struct sto_subsystem sto_subsystem_ ## SUBSYSTEM = {				\
	.name = # SUBSYSTEM,								\
	.op_table = (OP_TABLE),								\
	.ops = (OPS),									\
};											\
static void __attribute__((constructor)) sto_subsystem_ ## SUBSYSTEM ## _register(void)	\
{											\
	sto_add_subsystem(&sto_subsystem_ ## SUBSYSTEM);				\
}

#endif /* _STO_SUBSYSTEM_H_ */
