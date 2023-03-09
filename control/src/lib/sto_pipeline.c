#include "sto_pipeline.h"

#include <spdk/stdinc.h>
#include <spdk/thread.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/queue.h>

#include "sto_async.h"

#define STO_PL_ACTION_POLL_PERIOD	1000 /* 1ms */

enum sto_pipeline_action_type {
	STO_PL_ACTION_BASIC,
	STO_PL_ACTION_CONSTRUCTOR,
	STO_PL_ACTION_CNT,
};

struct sto_pipeline_action_hndl {
	enum sto_pipeline_action_type type;
	union {
		struct {
			sto_pl_action_basic_t fn;
		} basic;

		struct {
			sto_pl_action_constructor_t fn;
		} constructor;
	} u;
};

struct sto_pipeline_action {
	struct sto_pipeline_action_hndl hndl;
	struct sto_pipeline_action *rollback;

	struct sto_pipeline *pipe;

	TAILQ_ENTRY(sto_pipeline_action) list;
};

static struct sto_pipeline_action *
pipeline_action_alloc(struct sto_pipeline *pipe, struct sto_pipeline_action_hndl *hndl)
{
	struct sto_pipeline_action *action;

	action = calloc(1, sizeof(*action));
	if (spdk_unlikely(!action)) {
		SPDK_ERRLOG("Failed to alloc action\n");
		return NULL;
	}

	action->hndl = *hndl;
	action->pipe = pipe;

	return action;
}

static void pipeline_action_free(struct sto_pipeline_action *action);

static struct sto_pipeline_action *
pipeline_action_create(struct sto_pipeline *pipe, const struct sto_pipeline_step *step)
{
	struct sto_pipeline_action *action;
	struct sto_pipeline_action_hndl hndl = {}, rollback_hndl = {}, *rollback_hndl_ptr = NULL;

	switch (step->type) {
	case STO_PL_STEP_BASIC:
		hndl.type = STO_PL_ACTION_BASIC;
		hndl.u.basic.fn = step->u.basic.action_fn;

		if (step->u.basic.rollback_fn) {
			rollback_hndl.type = STO_PL_ACTION_BASIC;
			rollback_hndl.u.basic.fn = step->u.basic.rollback_fn;

			rollback_hndl_ptr = &rollback_hndl;
		}
		break;
	case STO_PL_STEP_CONSTRUCTOR:
		hndl.type = STO_PL_ACTION_CONSTRUCTOR;
		hndl.u.constructor.fn = step->u.constructor.action_fn;

		if (step->u.constructor.rollback_fn) {
			rollback_hndl.type = STO_PL_ACTION_CONSTRUCTOR;
			rollback_hndl.u.constructor.fn = step->u.constructor.rollback_fn;

			rollback_hndl_ptr = &rollback_hndl;
		}
		break;
	default:
		SPDK_ERRLOG("Failed to process pipeline step with typed %d\n",
			    step->type);
		return NULL;
	}

	action = pipeline_action_alloc(pipe, &hndl);
	if (spdk_unlikely(!action)) {
		SPDK_ERRLOG("Failed to alloc action for step\n");
		return NULL;
	}

	if (rollback_hndl_ptr) {
		action->rollback = pipeline_action_alloc(pipe, rollback_hndl_ptr);
		if (spdk_unlikely(!action->rollback)) {
			SPDK_ERRLOG("Failed to alloc rollback for step\n");
			goto free_action;
		}
	}

	return action;

free_action:
	pipeline_action_free(action);

	return NULL;
}

static void
pipeline_action_free(struct sto_pipeline_action *action)
{
	if (action->rollback) {
		pipeline_action_free(action->rollback);
		action->rollback = NULL;
	}

	free(action);
}

static void
pipeline_action_manage_rollback(struct sto_pipeline_action *action)
{
	struct sto_pipeline *pipe = action->pipe;

	if (spdk_likely(pipe->rollback)) {
		return;
	}

	if (pipe->cur_rollback) {
		TAILQ_INSERT_HEAD(&pipe->rollback_stack, pipe->cur_rollback, list);
		pipe->cur_rollback = NULL;
	}

	if (action->rollback) {
		pipe->cur_rollback = action->rollback;
		action->rollback = NULL;
	}
}

static void
pipeline_basic_action_execute(struct sto_pipeline_action *action)
{
	struct sto_pipeline *pipe = action->pipe;

	pipeline_action_manage_rollback(action);

	action->hndl.u.basic.fn(pipe);

	pipeline_action_free(action);
}

static void
pipeline_constructor_action_execute(struct sto_pipeline_action *action)
{
	struct sto_pipeline *pipe = action->pipe;
	int rc = 0;

	pipeline_action_manage_rollback(action);

	rc = action->hndl.u.constructor.fn(pipe);

	switch (rc) {
	case 0:
		TAILQ_INSERT_HEAD(&pipe->action_queue, action, list);
		break;
	case STO_PL_CONSTRUCTOR_FINISHED:
		rc = 0;
		/* fallthrough */
	default:
		pipeline_action_free(action);
		sto_pipeline_step_next(pipe, rc);
		break;
	};
}

static void
pipeline_action_execute(struct sto_pipeline_action *action)
{
	switch (action->hndl.type) {
	case STO_PL_ACTION_BASIC:
		pipeline_basic_action_execute(action);
		break;
	case STO_PL_ACTION_CONSTRUCTOR:
		pipeline_constructor_action_execute(action);
		break;
	default:
		SPDK_ERRLOG("Invalid type of action %d for pipeline %p\n",
			    action->hndl.type, action->pipe);
		assert(0);
		break;
	};
}

static int pipeline_action_poll(void *ctx);

struct sto_pipeline_engine *
sto_pipeline_engine_create(const char *name)
{
	struct sto_pipeline_engine *engine;

	engine = calloc(1, sizeof(*engine));
	if (spdk_unlikely(!engine)) {
		SPDK_ERRLOG("Failed to alloc %s pipeline engine\n", name);
		return NULL;
	}

	engine->name = strdup(name);
	if (spdk_unlikely(!engine->name)) {
		SPDK_ERRLOG("Failed to alloc name for pipeline engine %s\n", name);
		goto free_engine;
	}

	engine->poller = SPDK_POLLER_REGISTER(pipeline_action_poll, engine, STO_PL_ACTION_POLL_PERIOD);
	if (spdk_unlikely(!engine->poller)) {
		SPDK_ERRLOG("Cann't register the STO pipeline engine %s poller\n",
			    engine->name);
		goto free_name;
	}

	TAILQ_INIT(&engine->pipeline_list);

	return engine;

free_name:
	free((char *) engine->name);

free_engine:
	free(engine);

	return NULL;
}

void
sto_pipeline_engine_destroy(struct sto_pipeline_engine *engine)
{
	spdk_poller_unregister(&engine->poller);
	free((char *) engine->name);
	free(engine);
}

static void pipeline_type_deinit(struct sto_pipeline_type *type);

static int
pipeline_type_init(struct sto_pipeline_type *type, const struct sto_pipeline_properties *properties)
{
	int rc = 0;

	if (properties->ctx_size) {
		type->ctx = calloc(1, properties->ctx_size);
		if (spdk_unlikely(!type->ctx)) {
			SPDK_ERRLOG("Failed to alloc pipeline type ctx\n");
			rc = -ENOMEM;
			goto error;
		}

		type->ctx_deinit_fn = properties->ctx_deinit_fn;
	}

	return 0;

error:
	pipeline_type_deinit(type);

	return rc;
}

static void
pipeline_type_deinit(struct sto_pipeline_type *type)
{
	if (type->ctx) {
		if (type->ctx_deinit_fn) {
			type->ctx_deinit_fn(type->ctx);
		}

		free(type->ctx);
	}
}

int
sto_pipeline_init(struct sto_pipeline *pipe, const struct sto_pipeline_properties *properties)
{
	int rc;

	TAILQ_INIT(&pipe->action_queue);
	TAILQ_INIT(&pipe->action_queue_todo);
	TAILQ_INIT(&pipe->rollback_stack);

	if (!properties) {
		goto out;
	}

	rc = pipeline_type_init(&pipe->type, properties);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init STO pipeline\n");
		return rc;
	}

	rc = sto_pipeline_add_steps(pipe, properties->steps);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to add steps to pipeline\n");
		return rc;
	}

out:
	return 0;
}

static void pipeline_action_list_free(struct sto_pipeline_action_list *actions);

void
sto_pipeline_deinit(struct sto_pipeline *pipe)
{
	pipeline_type_deinit(&pipe->type);

	if (pipe->cur_rollback) {
		pipeline_action_free(pipe->cur_rollback);
	}

	pipeline_action_list_free(&pipe->action_queue);
	pipeline_action_list_free(&pipe->action_queue_todo);
	pipeline_action_list_free(&pipe->rollback_stack);
}

struct sto_pipeline *
sto_pipeline_alloc(const struct sto_pipeline_properties *properties)
{
	struct sto_pipeline *pipe;
	int rc;

	pipe = calloc(1, sizeof(*pipe));
	if (spdk_unlikely(!pipe)) {
		SPDK_ERRLOG("Failed to alloc STO pipeline\n");
		return NULL;
	}

	rc = sto_pipeline_init(pipe, properties);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init pipeline\n");
		goto free_pipeline;
	}

	pipe->auto_release = true;

	return pipe;

free_pipeline:
	sto_pipeline_free(pipe);

	return NULL;
}

void
sto_pipeline_free(struct sto_pipeline *pipe)
{
	sto_pipeline_deinit(pipe);
	free(pipe);
}

void
sto_pipeline_run(struct sto_pipeline_engine *engine,
		 struct sto_pipeline *pipe,
		 sto_generic_cb cb_fn, void *cb_arg)
{
	pipe->engine = engine;
	pipe->cb_fn = cb_fn;
	pipe->cb_arg = cb_arg;

	sto_pipeline_step_start(pipe);
}

void
sto_pipeline_alloc_and_run(struct sto_pipeline_engine *engine,
			   const struct sto_pipeline_properties *properties,
			   sto_generic_cb cb_fn, void *cb_arg,
			   void *priv)
{
	struct sto_pipeline *pipe;

	pipe = sto_pipeline_alloc(properties);
	if (spdk_unlikely(!pipe)) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	sto_pipeline_set_priv(pipe, priv);

	sto_pipeline_run(engine, pipe, cb_fn, cb_arg);
}

static void
pipeline_done(struct sto_pipeline *pipe)
{
	pipe->cb_fn(pipe->cb_arg, pipe->returncode);

	if (pipe->auto_release) {
		sto_pipeline_free(pipe);
	}
}

static void
pipeline_action_list_free(struct sto_pipeline_action_list *actions)
{
	struct sto_pipeline_action *action, *tmp;

	TAILQ_FOREACH_SAFE(action, actions, list, tmp) {
		TAILQ_REMOVE(actions, action, list);

		pipeline_action_free(action);
	}
}

int
sto_pipeline_add_steps(struct sto_pipeline *pipe, const struct sto_pipeline_step *steps)
{
	const struct sto_pipeline_step *step;

	for (step = steps; step && step->type != STO_PL_STEP_TERMINATOR; step++) {
		struct sto_pipeline_action *action;

		action = pipeline_action_create(pipe, step);
		if (spdk_unlikely(!action)) {
			SPDK_ERRLOG("Failed to create action\n");
			return -ENOMEM;
		}

		TAILQ_INSERT_TAIL(&pipe->action_queue, action, list);
	}

	return 0;
}

int
__sto_pipeline_insert_step(struct sto_pipeline *pipe, const struct sto_pipeline_step *step)
{
	struct sto_pipeline_action *action;

	action = pipeline_action_create(pipe, step);
	if (spdk_unlikely(!action)) {
		return -ENOMEM;
	}

	TAILQ_INSERT_HEAD(&pipe->action_queue_todo, action, list);

	return 0;
}

static inline struct sto_pipeline_action *
pipeline_next_action(struct sto_pipeline *pipe)
{
	struct sto_pipeline_action_list *actions;
	struct sto_pipeline_action *action = NULL;

	if (spdk_likely(!pipe->rollback)) {
		actions = &pipe->action_queue;
	} else {
		actions = &pipe->rollback_stack;
	}

	if (!TAILQ_EMPTY(actions)) {
		action = TAILQ_FIRST(actions);
		TAILQ_REMOVE(actions, action, list);
	}

	return action;
}

static void
pipeline_check_actions_todo(struct sto_pipeline *pipe)
{
	struct sto_pipeline_action_list action_queue_todo = TAILQ_HEAD_INITIALIZER(action_queue_todo);
	struct sto_pipeline_action *action, *tmp;

	if (TAILQ_EMPTY(&pipe->action_queue_todo)) {
		return;
	}

	TAILQ_SWAP(&action_queue_todo, &pipe->action_queue_todo, sto_pipeline_action, list);

	TAILQ_FOREACH_REVERSE_SAFE(action, &action_queue_todo, sto_pipeline_action_list, list, tmp) {
		TAILQ_REMOVE(&action_queue_todo, action, list);
		TAILQ_INSERT_HEAD(&pipe->action_queue, action, list);
	}
}

static void
pipeline_action_finish(struct sto_pipeline *pipe)
{
	if (!pipe->error || pipe->rollback) {
		goto done;
	}

	pipe->returncode = pipe->error;
	pipe->error = 0;

	if (!TAILQ_EMPTY(&pipe->rollback_stack)) {
		pipe->rollback = true;
		sto_pipeline_step_next(pipe, 0);
		return;
	}

done:
	pipeline_done(pipe);
}

static void
pipeline_action(struct sto_pipeline *pipe)
{
	struct sto_pipeline_action *action;

	if (spdk_unlikely(pipe->error)) {
		goto finish;
	}

	action = pipeline_next_action(pipe);
	if (!action) {
		goto finish;
	}

	pipeline_action_execute(action);

	pipeline_check_actions_todo(pipe);

	return;

finish:
	pipeline_action_finish(pipe);
}

static int
pipeline_action_poll(void *ctx)
{
	struct sto_pipeline_engine *engine = ctx;
	struct sto_pipeline_list pipeline_list = TAILQ_HEAD_INITIALIZER(pipeline_list);
	struct sto_pipeline *pipeline, *tmp;

	if (TAILQ_EMPTY(&engine->pipeline_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&pipeline_list, &engine->pipeline_list, sto_pipeline, list);

	TAILQ_FOREACH_SAFE(pipeline, &pipeline_list, list, tmp) {
		TAILQ_REMOVE(&pipeline_list, pipeline, list);

		pipeline_action(pipeline);
	}

	return SPDK_POLLER_BUSY;
}

void
sto_pipeline_step_next(struct sto_pipeline *pipe, int rc)
{
	struct sto_pipeline_engine *engine = pipe->engine;

	pipe->error = rc;
	TAILQ_INSERT_TAIL(&engine->pipeline_list, pipe, list);
}
