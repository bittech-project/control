#ifndef _STO_COMPONENT_H_
#define _STO_COMPONENT_H_

#include <spdk/queue.h>

struct sto_json_iter;

struct sto_core_component {
	const char *name;

	const struct sto_op_table *(*decode)(const struct sto_json_iter *iter);

	TAILQ_ENTRY(sto_core_component) list;
};

void sto_core_add_component(struct sto_core_component *component);

#define STO_CORE_REGISTER_COMPONENT(COMPONENT, DECODE)						\
static struct sto_core_component sto_core_component_ ## COMPONENT = {				\
	.name = # COMPONENT,									\
	.decode = DECODE,									\
};												\
static void __attribute__((constructor)) sto_core_component_ ## COMPONENT ## _regsiter(void)	\
{												\
	sto_core_add_component(&sto_core_component_ ## COMPONENT);				\
}

struct sto_core_component *sto_core_component_find(const char *name);
struct sto_core_component *sto_core_component_next(struct sto_core_component *component);

static inline struct sto_core_component *
sto_core_component_first(void)
{
	return sto_core_component_next(NULL);
}

#define STO_CORE_FOREACH_COMPONENT(component)			\
	for ((component) = sto_core_component_first();		\
	     (component);					\
	     (component) = sto_core_component_next((component)))

const struct sto_op_table *sto_core_component_decode(const struct sto_json_iter *iter);

#endif /* _STO_COMPONENT_H_ */
