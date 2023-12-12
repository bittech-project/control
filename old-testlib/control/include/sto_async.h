#ifndef _STO_ASYNC_H_
#define _STO_ASYNC_H_

#include <spdk/stdinc.h>
#include <spdk/likely.h>

typedef void (*sto_generic_cb)(void *cb_arg, int rc);

struct sto_generic_cpl {
	sto_generic_cb cb_fn;
	void *cb_arg;
};

static inline struct sto_generic_cpl *
sto_generic_cpl_alloc(sto_generic_cb cb_fn, void *cb_arg)
{
	struct sto_generic_cpl *cpl;

	cpl = calloc(1, sizeof(*cpl));
	if (spdk_unlikely(!cpl)) {
		return NULL;
	}

	cpl->cb_arg = cb_arg;
	cpl->cb_fn = cb_fn;

	return cpl;
}

static inline void
sto_generic_cpl_free(struct sto_generic_cpl *cpl)
{
	free(cpl);
}

static inline void
sto_generic_call_cpl(struct sto_generic_cpl *cpl, int rc)
{
	cpl->cb_fn(cpl->cb_arg, rc);

	sto_generic_cpl_free(cpl);
}

static inline void
sto_generic_cpl_cb(void *priv, int rc)
{
	sto_generic_call_cpl(priv, rc);
}

#endif /* _STO_ASYNC_H_ */
