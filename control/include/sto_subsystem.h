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

struct spdk_json_write_ctx;
typedef void (*sto_subsystem_dump_params_t)(void *priv, struct spdk_json_write_ctx *w);

struct sto_core_req;
typedef void (*sto_subsystem_send_done_t)(void *priv, struct sto_core_req *core_req);

struct sto_subsystem_args {
	void *priv;
	sto_subsystem_send_done_t done;
};

int sto_subsystem_send(const char *subsystem, const char *op,
		       void *params, sto_subsystem_dump_params_t dump_params,
		       struct sto_subsystem_args *args);

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
