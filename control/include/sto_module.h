#ifndef _STO_MODULE_H_
#define _STO_MODULE_H_

#include <spdk/queue.h>

#include "sto_hashtable.h"

struct sto_module {
	const char *name;
	const struct sto_hashtable *op_map;

	TAILQ_ENTRY(sto_module) list;
};

struct sto_module *sto_module_find(const char *name);

void sto_add_module(struct sto_module *module);

#define STO_MODULE_REGISTER(MODULE, OP_TABLE)						\
static struct sto_module sto_module_ ## MODULE = {					\
	.name = # MODULE,								\
};											\
static void __attribute__((constructor)) sto_module_ ## MODULE ## _register(void)	\
{											\
	const struct sto_hashtable *ht;							\
											\
	ht = sto_ops_map_alloc((OP_TABLE));						\
	assert(ht);									\
											\
	sto_module_ ## MODULE.op_map = ht;						\
											\
	sto_add_module(&sto_module_ ## MODULE);						\
}

#endif /* _STO_MODULE_H_ */
