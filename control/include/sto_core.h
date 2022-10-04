#ifndef _STO_CORE_H_
#define _STO_CORE_H_

struct sto_req;
typedef void (*sto_req_done_t)(struct sto_req *req);

struct sto_subsystem;

enum sto_req_state {
	STO_REQ_STATE_PARSE = 0,
	STO_REQ_STATE_EXEC,
	STO_REQ_STATE_DONE,
	STO_REQ_STATE_COUNT
};

struct sto_req {
	void *priv;
	sto_req_done_t req_done;

	enum sto_req_state state;
	struct sto_subsystem *subsystem;

	TAILQ_ENTRY(sto_req) list;
};

int sto_core_init(void);
void sto_core_fini(void);

const char *sto_req_state_name(enum sto_req_state state);

struct sto_req *sto_req_alloc(const char *subsystem);
void sto_req_free(struct sto_req *req);
void sto_req_init_cb(struct sto_req *req, sto_req_done_t req_done, void *priv);

static inline void
sto_req_set_state(struct sto_req *req, enum sto_req_state new_state)
{
	req->state = new_state;
}

int sto_req_submit(struct sto_req *req);

#endif /* _STO_CORE_H_ */
