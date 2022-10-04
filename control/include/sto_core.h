#ifndef _STO_CORE_H_
#define _STO_CORE_H_

struct sto_req;
typedef void (*sto_req_done_t)(struct sto_req *req);

struct sto_subsystem;

struct sto_req {
	void *priv;
	sto_req_done_t req_done;

	struct sto_subsystem *subsystem;

	TAILQ_ENTRY(sto_req) list;
};

int sto_core_init(void);
void sto_core_fini(void);

struct sto_req *sto_req_alloc(const char *subsystem);
void sto_req_free(struct sto_req *req);
void sto_req_init_cb(struct sto_req *req, sto_req_done_t req_done, void *priv);

int sto_req_submit(struct sto_req *req);

#endif /* _STO_CORE_H_ */
