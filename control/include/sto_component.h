#ifndef _STO_COMPONENT_H_
#define _STO_COMPONENT_H_

#include <spdk/queue.h>

struct sto_json_iter;

struct sto_core_component {
	const char *name;

	const struct sto_shash *(*get_ops_map)(const char *object_name);

	TAILQ_ENTRY(sto_core_component) list;

	bool internal;
};

struct sto_core_component *sto_core_component_find(const char *name, bool skip_internal);
void sto_core_add_component(struct sto_core_component *component);
const struct sto_shash *sto_core_component_decode(const struct sto_json_iter *iter,
						  bool internal_user);

#define STO_CORE_REGISTER_INTERNAL_COMPONENT(COMPONENT, GET_OPS_MAP_FN)	\
	__STO_CORE_REGISTER_COMPONENT(COMPONENT, GET_OPS_MAP_FN, true)

#define STO_CORE_REGISTER_COMPONENT(COMPONENT, GET_OPS_MAP_FN)	\
	__STO_CORE_REGISTER_COMPONENT(COMPONENT, GET_OPS_MAP_FN, false)

#define __STO_CORE_REGISTER_COMPONENT(COMPONENT, GET_OPS_MAP_FN, INTERNAL)			\
static struct sto_core_component sto_core_component_ ## COMPONENT = {				\
	.name = # COMPONENT,									\
	.get_ops_map = GET_OPS_MAP_FN,								\
	.internal = INTERNAL,									\
};												\
static void __attribute__((constructor)) sto_core_component_ ## COMPONENT ## _regsiter(void)	\
{												\
	sto_core_add_component(&sto_core_component_ ## COMPONENT);				\
}

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

#endif /* _STO_COMPONENT_H_ */
