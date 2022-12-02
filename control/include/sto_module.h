#ifndef _STO_MODULE_H_
#define _STO_MODULE_H_

#include <spdk/queue.h>

struct sto_op_table;

struct sto_module {
	const char *name;

	const struct sto_op_table *op_table;

	TAILQ_ENTRY(sto_module) list;
};
#define STO_MODULE_INITIALIZER(name, op_table) {name, op_table}

void sto_add_module(struct sto_module *module);
struct sto_module *sto_module_find(const char *name);

#define STO_MODULE_REGISTER(_name)					\
static void __attribute__((constructor)) _name ## _register(void)	\
{									\
	sto_add_module(&_name);						\
}

#endif /* _STO_MODULE_H_ */
