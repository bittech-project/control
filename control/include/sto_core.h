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
void sto_core_req_process(struct sto_core_req *req);

void sto_core_req_end_response(struct sto_core_req *req, struct spdk_json_write_ctx *w);

#endif /* _STO_CORE_H_ */
