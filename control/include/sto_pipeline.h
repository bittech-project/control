#ifndef _STO_PIPELINE_H_
#define _STO_PIPELINE_H_

#include <spdk/queue.h>
#include <spdk/util.h>

#include "sto_async.h"
#include "sto_lib.h"

#define STO_PL_CONSTRUCTOR_FINISHED INT_MAX

struct sto_pipeline_action;
TAILQ_HEAD(sto_pipeline_action_list, sto_pipeline_action);

typedef void (*sto_pipeline_ctx_deinit_t)(void *ctx);

struct sto_pipeline_type {
	void *ctx;
	sto_pipeline_ctx_deinit_t ctx_deinit_fn;
};

struct sto_pipeline_engine;

struct sto_pipeline {
	struct sto_pipeline_type type;
	struct sto_pipeline_engine *engine;
	void *priv;

	int error;
	int returncode;

	bool rollback;
	bool auto_release;

	struct sto_pipeline_action *cur_rollback;

	struct sto_pipeline_action_list action_queue;
	struct sto_pipeline_action_list action_queue_todo;
	struct sto_pipeline_action_list rollback_stack;

	TAILQ_ENTRY(sto_pipeline) list;

	sto_generic_cb cb_fn;
	void *cb_arg;
};

typedef void (*sto_pl_action_basic_t)(struct sto_pipeline *pipe);
typedef int (*sto_pl_action_constructor_t)(struct sto_pipeline *pipe);

enum sto_pipeline_step_type {
	STO_PL_STEP_BASIC,
	STO_PL_STEP_CONSTRUCTOR,
	STO_PL_STEP_TERMINATOR,
	STO_PL_STEP_CNT,
};

struct sto_pipeline_step {
	enum sto_pipeline_step_type type;

	union {
		struct {
			sto_pl_action_basic_t action_fn;
			sto_pl_action_basic_t rollback_fn;
		} basic;

		struct {
			sto_pl_action_constructor_t action_fn;
			sto_pl_action_constructor_t rollback_fn;
		} constructor;

		struct {
		} terminator;
	} u;
};

#define STO_PL_STEP(_action_fn, _rollback_fn)		\
	{						\
		.type = STO_PL_STEP_BASIC,		\
		.u.basic.action_fn = _action_fn,	\
		.u.basic.rollback_fn = _rollback_fn,	\
	}

#define STO_PL_STEP_CONSTRUCTOR(_constructor_fn, _destructor_fn)	\
	{								\
		.type = STO_PL_STEP_CONSTRUCTOR,			\
		.u.constructor.action_fn = _constructor_fn,		\
		.u.constructor.rollback_fn = _destructor_fn,		\
	}

#define STO_PL_STEP_TERMINATOR()		\
	{					\
		.type = STO_PL_STEP_TERMINATOR,	\
	}

struct sto_pipeline_properties {
	size_t ctx_size;
	sto_pipeline_ctx_deinit_t ctx_deinit_fn;

	struct sto_pipeline_step steps[];
};

TAILQ_HEAD(sto_pipeline_list, sto_pipeline);

struct sto_pipeline_engine {
	const char *name;
	struct spdk_poller *poller;
	struct sto_pipeline_list pipeline_list;
};

struct sto_pipeline_engine *sto_pipeline_engine_create(const char *name);
void sto_pipeline_engine_destroy(struct sto_pipeline_engine *engine);

int sto_pipeline_init(struct sto_pipeline *pipe, const struct sto_pipeline_properties *properties);
void sto_pipeline_deinit(struct sto_pipeline *pipe);

struct sto_pipeline *sto_pipeline_alloc(const struct sto_pipeline_properties *properties);
void sto_pipeline_free(struct sto_pipeline *pipe);
void sto_pipeline_run(struct sto_pipeline_engine *engine,
		      struct sto_pipeline *pipe,
		      sto_generic_cb cb_fn, void *cb_arg);

void sto_pipeline_alloc_and_run(struct sto_pipeline_engine *engine,
				const struct sto_pipeline_properties *properties,
				sto_generic_cb cb_fn, void *cb_arg,
				void *priv);

static inline void *
sto_pipeline_get_ctx(struct sto_pipeline *pipe)
{
	return pipe->type.ctx;
}

static inline void
sto_pipeline_set_priv(struct sto_pipeline *pipe, void *priv)
{
	pipe->priv = priv;
}

static inline void *
sto_pipeline_get_priv(struct sto_pipeline *pipe)
{
	return pipe->priv;
}

void sto_pipeline_step_next(struct sto_pipeline *pipe, int rc);

static inline void
sto_pipeline_step_start(struct sto_pipeline *pipe)
{
	sto_pipeline_step_next(pipe, 0);
}

static inline void
sto_pipeline_step_done(void *cb_arg, int rc)
{
	sto_pipeline_step_next(cb_arg, rc);
}

int sto_pipeline_add_steps(struct sto_pipeline *pipe, const struct sto_pipeline_step *steps);

int __sto_pipeline_insert_step(struct sto_pipeline *pipe, const struct sto_pipeline_step *step);

#define sto_pipeline_insert_step(_pipe, _step)	\
	__sto_pipeline_insert_step(_pipe, &(const struct sto_pipeline_step) _step)

#define sto_pipeline_queue_step(_pipe, _step)	\
	sto_pipeline_step_next(_pipe, __sto_pipeline_insert_step(_pipe, &(const struct sto_pipeline_step) _step))


#endif /* _STO_PIPELINE_H_ */
