#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

#include "sto_core.h"

struct spdk_json_val;

typedef void (*sto_subsys_init_t)(void);
typedef void (*sto_subsys_fini_t)(void);

typedef void *(*sto_subsys_alloc_req_t)(const struct spdk_json_val *params);
typedef int (*sto_subsys_exec_req_t)(void *req_arg, sto_response_cb_t resp_cb, void *priv);
typedef void (*sto_subsys_done_req_t)(void *req_arg);

struct sto_subsystem {
	const char *name;

	sto_subsys_init_t init;
	sto_subsys_fini_t fini;

	sto_subsys_alloc_req_t alloc_req;

	sto_subsys_exec_req_t exec_req;
	sto_subsys_done_req_t done_req;

	TAILQ_ENTRY(sto_subsystem) list;
};

void sto_add_subsystem(struct sto_subsystem *subsystem);

struct sto_subsystem *sto_subsystem_find(const char *name);


#define STO_SUBSYSTEM_REGISTER(_name)					\
static void __attribute__((constructor)) _name ## _register(void)	\
{									\
	_name.init();							\
	sto_add_subsystem(&_name);					\
}									\
									\
static void __attribute__((destructor)) _name ## _deregister(void)	\
{									\
	_name.fini();							\
}

#endif /* _STO_SUBSYSTEM_H_ */
