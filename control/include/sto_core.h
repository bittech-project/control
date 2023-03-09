#ifndef _STO_CORE_H_
#define _STO_CORE_H_

#include <spdk/stdinc.h>
#include <spdk/queue.h>

#include "sto_lib.h"

struct spdk_json_write_ctx;
struct sto_core_req;
struct spdk_json_val;
struct sto_req;

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

	void *priv;
	sto_core_req_done_t done;

	TAILQ_ENTRY(sto_core_req) list;

	bool internal;
};

typedef void (*sto_core_init_fn)(void *cb_arg, int rc);
typedef void (*sto_core_fini_fn)(void *cb_arg);

void sto_core_init(sto_core_init_fn cb_fn, void *cb_arg);
void sto_core_fini(sto_core_fini_fn cb_fn, void *cb_arg);

const char *sto_core_req_state_name(enum sto_core_req_state state);

void sto_core_req_free(struct sto_core_req *req);

int sto_core_process(const struct spdk_json_val *params, sto_core_req_done_t done, void *priv);
int sto_core_process_raw(const struct sto_json_head_raw *head,
			 sto_core_req_done_t done, void *priv);

static inline void
sto_core_req_set_state(struct sto_core_req *req, enum sto_core_req_state new_state)
{
	req->state = new_state;
}

void sto_core_req_response(struct sto_core_req *req, struct spdk_json_write_ctx *w);

#endif /* _STO_CORE_H_ */
