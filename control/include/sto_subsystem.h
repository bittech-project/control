#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

struct spdk_json_val;
struct sto_req;

typedef void *(*sto_subsys_alloc_req_t)(const struct spdk_json_val *params);

struct sto_subsystem {
	const char *name;

	sto_subsys_alloc_req_t alloc_req;

	void (*init_req)(void *req_arg, void (*exec_done)(void *priv), void *priv);
	int (*exec_req)(void *req_arg);
	void (*done_req)(void *req_arg);

	TAILQ_ENTRY(sto_subsystem) list;
};

void sto_add_subsystem(struct sto_subsystem *subsystem);

struct sto_subsystem *sto_subsystem_find(const char *name);


#define STO_SUBSYSTEM_REGISTER(_name)					\
static void __attribute__((constructor)) _name ## _register(void)	\
{									\
	sto_add_subsystem(&_name);					\
}

#endif /* _STO_SUBSYSTEM_H_ */
