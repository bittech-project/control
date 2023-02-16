#ifndef _STO_ASYNC_H_
#define _STO_ASYNC_H_

typedef void (*sto_generic_cb)(void *cb_arg, int rc);

struct sto_generic_cb_ctx {
	void *cb_arg;
	sto_generic_cb cb_fn;
};

static inline struct sto_generic_cb_ctx *
sto_generic_cb_ctx_alloc(void *cb_arg, sto_generic_cb cb_fn)
{
	struct sto_generic_cb_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (spdk_unlikely(!ctx)) {
		return NULL;
	}

	ctx->cb_arg = cb_arg;
	ctx->cb_fn = cb_fn;

	return ctx;
}

static inline void
sto_generic_cb_ctx_done(struct sto_generic_cb_ctx *ctx, int rc)
{
	ctx->cb_fn(ctx->cb_arg, rc);
	free(ctx);
}

#endif /* _STO_ASYNC_H_ */
