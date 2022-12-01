#ifndef _STO_MODULE_H_
#define _STO_MODULE_H_

#include <spdk/queue.h>

#include "sto_core.h"

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

struct sto_module_req;
typedef int (*sto_module_transition_t)(struct sto_module_req *module_req);

typedef struct {
	sto_module_transition_t *transitions;
	size_t transition_num;
} sto_module_tt;

#define STO_MODULE_TT_INITIALIZER(tt) {tt, SPDK_COUNTOF(tt)}

struct sto_module_req {
	struct sto_req req;

	sto_module_tt *tt;
	const struct spdk_json_val *cdb;

	int phase;
	int returncode;

	TAILQ_ENTRY(sto_module_req) list;
};

struct sto_module_req_params_constructor {
	sto_module_tt *tt;
};

extern struct sto_req_ops sto_module_req_ops;

STO_REQ_CONSTRUCTOR_DECLARE(module)

void sto_module_req_process(struct sto_module_req *module_req);

#endif /* _STO_MODULE_H_ */
