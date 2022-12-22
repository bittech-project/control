#ifndef _STO_MODULE_H_
#define _STO_MODULE_H_

#include <spdk/queue.h>

struct sto_op_table;

struct sto_module {
	const char *name;

	const struct sto_op_table *op_table;

	TAILQ_ENTRY(sto_module) list;
};

struct sto_module *sto_module_find(const char *name);

void sto_add_module(struct sto_module *module);

#define STO_MODULE_REGISTER(MODULE, OP_TABLE)						\
static struct sto_module sto_module_ ## MODULE = {					\
	.name = # MODULE,								\
	.op_table = OP_TABLE,								\
};											\
static void __attribute__((constructor)) sto_module_ ## MODULE ## _register(void)	\
{											\
	sto_add_module(&sto_module_ ## MODULE);						\
}

#endif /* _STO_MODULE_H_ */
