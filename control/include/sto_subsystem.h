#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

struct sto_response;
struct spdk_json_val;

typedef void (*sto_subsys_response_t)(void *priv, struct sto_response *resp);

typedef void (*sto_subsys_init_t)(void);
typedef void (*sto_subsys_fini_t)(void);

typedef struct sto_context *(*sto_subsys_parse_t)(const struct spdk_json_val *params);
typedef int (*sto_subsys_exec_t)(struct sto_context *ctx);
typedef void (*sto_subsys_free_t)(struct sto_context *ctx);

struct sto_subsystem {
	const char *name;

	sto_subsys_init_t init;
	sto_subsys_fini_t fini;

	sto_subsys_parse_t parse;
	sto_subsys_exec_t exec;
	sto_subsys_free_t free;

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
