#ifndef _STO_CORE_H_
#define _STO_CORE_H_

#include <spdk/queue.h>

#include "sto_lib.h"

struct spdk_json_write_ctx;
struct sto_core_req;

typedef void (*sto_core_req_done_t)(struct sto_core_req *req);

enum sto_core_req_state {
	STO_CORE_REQ_STATE_PARSE = 0,
	STO_CORE_REQ_STATE_EXEC,
	STO_CORE_REQ_STATE_DONE,
	STO_CORE_REQ_STATE_COUNT
};

struct sto_core_req {
	const struct spdk_json_val *params;

	enum sto_core_req_state state;

	struct sto_req_context *req_ctx;
	struct sto_err_context err_ctx;

	TAILQ_ENTRY(sto_core_req) list;

	void *priv;
	sto_core_req_done_t done;
};

typedef const struct sto_ops *(*sto_core_component_decode_ops_t)(const struct spdk_json_val *params,
								 const struct spdk_json_val **params_cdb);

struct sto_core_component {
	const char *name;

	sto_core_component_decode_ops_t decode_ops;

	TAILQ_ENTRY(sto_core_component) list;
};

#define STO_CORE_COMPONENT_INITIALIZER(name, decode_ops) {name, decode_ops}

void sto_core_add_component(struct sto_core_component *component);

#define STO_CORE_COMPONENT_REGISTER(_name)				\
static void __attribute__((constructor)) _name ## _register(void)	\
{									\
	sto_core_add_component(&_name);					\
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

int sto_core_init(void);
void sto_core_fini(void);

const char *sto_core_req_state_name(enum sto_core_req_state state);

void sto_core_req_free(struct sto_core_req *req);

struct sto_core_args {
	void *priv;
	sto_core_req_done_t done;
};

int sto_core_process_start(const struct spdk_json_val *params, struct sto_core_args *args);

static inline void
sto_core_req_set_state(struct sto_core_req *req, enum sto_core_req_state new_state)
{
	req->state = new_state;
}

void sto_core_req_response(struct sto_core_req *req, struct spdk_json_write_ctx *w);

#endif /* _STO_CORE_H_ */
