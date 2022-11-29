#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

#include <spdk/queue.h>

struct sto_ops;
struct spdk_json_val;

typedef void (*sto_subsys_response_t)(void *priv);

typedef const struct sto_ops *(*sto_subsystem_find_ops)(const char *name);

struct sto_subsystem {
	const char *name;

	sto_subsystem_find_ops find_ops;

	TAILQ_ENTRY(sto_subsystem) list;
};

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
