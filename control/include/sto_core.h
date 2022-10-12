#ifndef _STO_CORE_H_
#define _STO_CORE_H_

#include "sto_subsystem.h"

struct spdk_json_write_ctx;
struct sto_req;
typedef void (*sto_req_response_t)(struct sto_req *req);

struct sto_subsystem;

enum sto_req_state {
	STO_REQ_STATE_PARSE = 0,
	STO_REQ_STATE_EXEC,
	STO_REQ_STATE_RESPONSE,
	STO_REQ_STATE_COUNT
};

struct sto_response {
	int resultcode;
	char *buf;
};

struct sto_cdbops {
	int ops;
	const char *name;
};

struct sto_context {
	void *priv;
	sto_subsys_response_t response;
};

struct sto_req {
	void *priv;
	sto_req_response_t response;

	const struct spdk_json_val *params;

	enum sto_req_state state;

	struct sto_subsystem *subsystem;
	struct sto_context *ctx;

	struct sto_response *resp;

	TAILQ_ENTRY(sto_req) list;
};

int sto_core_init(void);
void sto_core_fini(void);

const char *sto_req_state_name(enum sto_req_state state);

struct sto_req *sto_req_alloc(const struct spdk_json_val *params);
void sto_req_init_cb(struct sto_req *req, sto_req_response_t response, void *priv);
void sto_req_free(struct sto_req *req);

static inline void
sto_req_set_state(struct sto_req *req, enum sto_req_state new_state)
{
	req->state = new_state;
}

void sto_req_submit(struct sto_req *req);
void sto_req_process(struct sto_req *req);

int sto_decode_object_str(const struct spdk_json_val *values,
			  const char *name, char **value);
const struct spdk_json_val *sto_decode_next_cdb(const struct spdk_json_val *params);

struct sto_response *sto_response_alloc(int resultcode, const char *fmt, ...);
void sto_response_free(struct sto_response *resp);

void sto_response_dump_json(struct sto_response *resp, struct spdk_json_write_ctx *w);

#endif /* _STO_CORE_H_ */
