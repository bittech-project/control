#ifndef _STO_COMPONENT_H_
#define _STO_COMPONENT_H_

#include <spdk/stdinc.h>
#include <spdk/queue.h>

struct sto_json_iter;

struct sto_core_component {
	const char *name;

	const struct sto_shash *(*get_ops_map)(const char *object_name);
	void (*init)(void);
	void (*fini)(void);

	bool internal;

	TAILQ_ENTRY(sto_core_component) list;
};

struct sto_core_component *sto_core_component_find(const char *name, bool skip_internal);
void sto_core_add_component(struct sto_core_component *component);
const struct sto_shash *sto_core_component_decode(const struct sto_json_iter *iter,
						  bool internal_user);

typedef void (*sto_core_component_init_fn)(void *cb_arg, int rc);
typedef void (*sto_core_component_fini_fn)(void *cb_arg);

void sto_core_component_init(sto_core_component_init_fn cb_fn, void *cb_arg);
void sto_core_component_init_next(int rc);

void sto_core_component_fini(sto_core_component_fini_fn cb_fn, void *cb_arg);
void sto_core_component_fini_next(void);

#define STO_CORE_COMPONENT_REGISTER(_name)							\
static void __attribute__((constructor)) sto_core_component_ ## _name ## _regsiter(void)	\
{												\
	sto_core_add_component(&_name);								\
}

#endif /* _STO_COMPONENT_H_ */
