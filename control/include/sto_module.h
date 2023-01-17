#ifndef _STO_MODULE_H_
#define _STO_MODULE_H_

#include <spdk/queue.h>

#include "sto_hash.h"

struct sto_op_table;

struct sto_module {
	const char *name;
	const struct sto_shash ops_map;
	const struct sto_op_table *op_table;

	TAILQ_ENTRY(sto_module) list;
};

struct sto_module *sto_module_find(const char *name);
void sto_add_module(struct sto_module *module);

#define STO_MODULE_REGISTER(MODULE, OP_TABLE)						\
static struct sto_module sto_module_ ## MODULE = {					\
	.name = # MODULE,								\
	.op_table = (OP_TABLE),								\
};											\
static void __attribute__((constructor)) sto_module_ ## MODULE ## _register(void)	\
{											\
	assert(!sto_ops_map_init(&sto_module_ ## MODULE.ops_map, (OP_TABLE)));		\
	sto_add_module(&sto_module_ ## MODULE);						\
}

struct sto_module *sto_module_next(struct sto_module *module);

static inline struct sto_module *
sto_module_first(void)
{
	return sto_module_next(NULL);
}

#define STO_FOREACH_MODULE(module)			\
	for ((module) = sto_module_first();		\
	     (module);					\
	     (module) = sto_module_next((module)))


#endif /* _STO_MODULE_H_ */
