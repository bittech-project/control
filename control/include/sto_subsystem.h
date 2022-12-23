#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

#include <spdk/queue.h>

struct sto_op_table;

struct sto_subsystem {
	const char *name;

	const struct sto_op_table *op_table;

	TAILQ_ENTRY(sto_subsystem) list;
};

struct sto_subsystem *sto_subsystem_find(const char *name);

void sto_add_subsystem(struct sto_subsystem *subsystem);

#define STO_SUBSYSTEM_REGISTER(SUBSYSTEM, OP_TABLE)					\
static struct sto_subsystem sto_subsystem_ ## SUBSYSTEM = {				\
	.name = # SUBSYSTEM,								\
	.op_table = OP_TABLE,								\
};											\
static void __attribute__((constructor)) sto_subsystem_ ## SUBSYSTEM ## _register(void)	\
{											\
	sto_add_subsystem(&sto_subsystem_ ## SUBSYSTEM);				\
}

#endif /* _STO_SUBSYSTEM_H_ */
