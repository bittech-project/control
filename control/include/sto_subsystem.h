#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

#include <spdk/queue.h>

#include "sto_hash.h"

struct sto_subsystem {
	const char *name;
	const struct sto_shash ops_map;

	TAILQ_ENTRY(sto_subsystem) list;
};

struct sto_subsystem *sto_subsystem_find(const char *name);

void sto_add_subsystem(struct sto_subsystem *subsystem);

#define STO_SUBSYSTEM_REGISTER(SUBSYSTEM, OP_TABLE)					\
static struct sto_subsystem sto_subsystem_ ## SUBSYSTEM = {				\
	.name = # SUBSYSTEM,								\
};											\
static void __attribute__((constructor)) sto_subsystem_ ## SUBSYSTEM ## _register(void)	\
{											\
	assert(!sto_ops_map_init(&sto_subsystem_ ## SUBSYSTEM.ops_map, (OP_TABLE)));	\
	sto_add_subsystem(&sto_subsystem_ ## SUBSYSTEM);				\
}

#endif /* _STO_SUBSYSTEM_H_ */
