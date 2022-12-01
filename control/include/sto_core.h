#ifndef _STO_CORE_H_
#define _STO_CORE_H_

#include <spdk/queue.h>

#include "sto_lib.h"

struct spdk_json_write_ctx;
struct sto_core_req;

typedef void (*sto_core_req_response_t)(struct sto_core_req *req);

enum sto_core_req_state {
	STO_CORE_REQ_STATE_PARSE = 0,
	STO_CORE_REQ_STATE_EXEC,
	STO_CORE_REQ_STATE_RESPONSE,
	STO_CORE_REQ_STATE_COUNT
};

struct sto_core_req {
	void *priv;
	sto_core_req_response_t response;

	const struct spdk_json_val *params;

	enum sto_core_req_state state;

	struct sto_context *ctx;
	struct sto_err_context err_ctx;

	TAILQ_ENTRY(sto_core_req) list;
};

typedef int (*sto_core_component_init_t)(void);
typedef void (*sto_core_component_fini_t)(void);
typedef const struct sto_ops *(*sto_core_component_decode_ops_t)(const struct spdk_json_val *params,
								 const struct spdk_json_val **params_cdb);

struct sto_core_component {
	const char *name;

	sto_core_component_init_t init;
	sto_core_component_fini_t fini;
	sto_core_component_decode_ops_t decode_ops;

	bool initialized;

	TAILQ_ENTRY(sto_core_component) list;
};

#define STO_CORE_COMPONENT_INITIALIZER(name, init, fini, decode_ops) {name, init, fini, decode_ops}

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

struct sto_core_req *sto_core_req_alloc(const struct spdk_json_val *params);
void sto_core_req_init_cb(struct sto_core_req *req, sto_core_req_response_t response, void *priv);
void sto_core_req_free(struct sto_core_req *req);

static inline void
sto_core_req_set_state(struct sto_core_req *req, enum sto_core_req_state new_state)
{
	req->state = new_state;
}

void sto_core_req_submit(struct sto_core_req *req);

void sto_core_req_end_response(struct sto_core_req *req, struct spdk_json_write_ctx *w);

#endif /* _STO_CORE_H_ */
