#ifndef _STO_REQ_H_
#define _STO_REQ_H_

#include <spdk/queue.h>
#include <spdk/util.h>

#include "sto_lib.h"
#include "sto_pipeline.h"
#include "sto_core.h"

typedef void (*sto_req_context_done_t)(void *priv);

struct sto_req_context {
	void *priv;
	sto_req_context_done_t done;
	struct sto_err_context *err_ctx;
};

struct sto_req;

typedef void (*sto_req_params_deinit_t)(void *priv);
typedef void (*sto_req_priv_deinit_t)(void *priv);

typedef void (*sto_req_response_t)(struct sto_req *req, struct spdk_json_write_ctx *w);

struct sto_req_properties {
	size_t params_size;
	sto_req_params_deinit_t params_deinit_fn;

	size_t priv_size;
	sto_req_priv_deinit_t priv_deinit_fn;

	sto_req_response_t response;
	struct sto_pipeline_step steps[];
};

struct sto_req_type {
	void *params;
	sto_req_params_deinit_t params_deinit_fn;

	void *priv;
	sto_req_priv_deinit_t priv_deinit_fn;

	sto_req_response_t response;
};

struct sto_req {
	struct sto_req_context ctx;
	struct sto_req_type type;

	struct sto_pipeline pipeline;
};

int sto_req_lib_init(void);
void sto_req_lib_fini(void);

void sto_req_run(struct sto_req *req);

static inline struct sto_req *
sto_req_from_ctx(struct sto_req_context *req_ctx)
{
	return SPDK_CONTAINEROF(req_ctx, struct sto_req, ctx);
}

static inline void *
sto_req_get_priv(struct sto_req *req)
{
	return req->type.priv;
}

static inline void *
sto_req_get_params(struct sto_req *req)
{
	return req->type.params;
}

static inline void
sto_req_done(void *cb_arg, int rc)
{
	struct sto_req *req = cb_arg;
	struct sto_req_context *req_ctx = &req->ctx;

	if (spdk_unlikely(rc)) {
		sto_err(req_ctx->err_ctx, rc);
	}

	req_ctx->done(req_ctx->priv);
}

struct sto_req *sto_req_alloc(const struct sto_req_properties *properties);
int sto_req_type_parse_params(struct sto_req_type *type,
			      const struct sto_ops_params_properties *properties,
			      const struct sto_json_iter *iter,
			      sto_ops_req_params_constructor_t params_constructor);
void sto_req_free(struct sto_req *req);

int sto_req_core_submit(struct sto_req *req, sto_core_req_done_t done,
			const struct sto_json_head_raw *head);
#endif /* _STO_REQ_H_ */
