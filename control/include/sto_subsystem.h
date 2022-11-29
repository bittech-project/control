#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

#include <spdk/queue.h>

struct sto_ops;
struct sto_op_table;
struct spdk_json_val;

typedef void (*sto_subsys_response_t)(void *priv);

struct sto_subsystem {
	const char *name;

	const struct sto_op_table *op_table;

	TAILQ_ENTRY(sto_subsystem) list;
};
#define STO_SUBSYSTEM_INITIALIZER(name, op_table) {name, op_table}

void sto_add_subsystem(struct sto_subsystem *subsystem);

struct sto_subsystem *sto_subsystem_find(const char *name);
struct sto_context *sto_subsystem_parse(struct sto_subsystem *subsystem,
					const struct spdk_json_val *params);

#define STO_SUBSYSTEM_REGISTER(_name)					\
static void __attribute__((constructor)) _name ## _register(void)	\
{									\
	sto_add_subsystem(&_name);					\
}

#endif /* _STO_SUBSYSTEM_H_ */
