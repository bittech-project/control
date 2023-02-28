#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include "sto_pipeline.h"
#include "sto_err.h"

#define STO_PL_ACTION_POLL_PERIOD	1000 /* 1ms */

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
static void pipeline_action_free(struct sto_pipeline_action *action);

void
sto_pipeline_deinit(struct sto_pipeline *pipe)
{
	pipeline_type_deinit(&pipe->type);

	if (pipe->cur_rollback) {
		pipeline_action_free(pipe->cur_rollback);
	}

	pipeline_action_list_free(&pipe->action_queue);
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

static int
pipeline_action_execute(struct sto_pipeline_action *action, struct sto_pipeline *pipe)
{
	int rc = 0;

	if (pipe->cur_rollback) {
		TAILQ_INSERT_HEAD(&pipe->rollback_stack, pipe->cur_rollback, list);
	}

	pipe->cur_rollback = action->priv;
	action->priv = NULL;

	rc = action->fn(pipe);

	pipeline_action_free(action);

	return rc;
}

static void
pipeline_action_priv_free(void *priv)
{
	struct sto_pipeline_action *rollback = priv;

	pipeline_action_free(rollback);
}

static int
pipeline_rollback_execute(struct sto_pipeline_action *action, struct sto_pipeline *pipe)
{
	int rc = 0;

	rc = action->fn(pipe);

	pipeline_action_free(action);

	return rc;
}

static int
pipeline_constructor_execute(struct sto_pipeline_action *constructor, struct sto_pipeline *pipe)
{
	int rc = 0;

	if (pipe->cur_rollback) {
		TAILQ_INSERT_HEAD(&pipe->rollback_stack, pipe->cur_rollback, list);
		pipe->cur_rollback = NULL;
	}

	if (constructor->priv) {
		pipe->cur_rollback = constructor->priv;
		constructor->priv = NULL;
	}

	rc = constructor->fn(pipe);

	switch (rc) {
	case 0:
		TAILQ_INSERT_HEAD(&pipe->action_queue, constructor, list);
		break;
	case STO_PL_CONSTRUCTOR_FINISHED:
		sto_pipeline_step_next(pipe, 0);
		rc = 0;
		/* fallthrough */
	default:
		pipeline_action_free(constructor);
		break;
	};

	return rc;
}

static void
pipeline_constructor_priv_free(void *priv)
{
	struct sto_pipeline_action *destructor = priv;

	pipeline_action_free(destructor);
}

static int
pipeline_destructor_execute(struct sto_pipeline_action *destructor, struct sto_pipeline *pipe)
{
	int rc = 0;

	rc = destructor->fn(pipe);

	switch (rc) {
	case 0:
		TAILQ_INSERT_HEAD(&pipe->action_queue, destructor, list);
		break;
	case STO_PL_CONSTRUCTOR_FINISHED:
		sto_pipeline_step_next(pipe, 0);
		rc = 0;
		/* fallthrough */
	default:
		pipeline_action_free(destructor);
		break;
	};

	return rc;
}

static struct sto_pipeline_action *
pipeline_action_alloc(enum sto_pipeline_action_type type,
		      sto_pipeline_action_t action_fn, sto_pipeline_action_t rollback_fn)
{
	struct sto_pipeline_action *action;

	assert(action_fn);

	action = calloc(1, sizeof(*action));
	if (spdk_unlikely(!action)) {
		SPDK_ERRLOG("Failed to alloc action\n");
		return NULL;
	}

	action->fn = action_fn;

	switch (type) {
	case STO_PL_ACTION_NORMAL:
		action->execute = pipeline_action_execute;

		if (!rollback_fn) {
			break;
		}

		action->priv = pipeline_action_alloc(STO_PL_ACTION_ROLLBACK, rollback_fn, NULL);
		if (spdk_unlikely(!action->priv)) {
			SPDK_ERRLOG("Failed to alloc priv for action\n");
			goto free_action;
		}

		action->priv_free = pipeline_action_priv_free;

		break;
	case STO_PL_ACTION_ROLLBACK:
		action->execute = pipeline_rollback_execute;
		break;
	case STO_PL_ACTION_CONSTRUCTOR:
		action->execute = pipeline_constructor_execute;

		if (!rollback_fn) {
			break;
		}

		action->priv = pipeline_action_alloc(STO_PL_ACTION_DESTRUCTOR, rollback_fn, NULL);
		if (spdk_unlikely(!action->priv)) {
			SPDK_ERRLOG("Failed to alloc priv for constructor\n");
			goto free_action;
		}

		action->priv_free = pipeline_constructor_priv_free;

		break;
	case STO_PL_ACTION_DESTRUCTOR:
		action->execute = pipeline_destructor_execute;
		break;
	default:
		SPDK_ERRLOG("Unknown action type %d\n", type);
		goto free_action;
	}

	return action;

free_action:
	free(action);

	return NULL;
}

static void
pipeline_action_free(struct sto_pipeline_action *action)
{
	if (action->priv && action->priv_free) {
		action->priv_free(action->priv);
	}

	free(action);
}

int
sto_pipeline_add_action(struct sto_pipeline *pipe, enum sto_pipeline_action_type type,
			sto_pipeline_action_t action_fn, sto_pipeline_action_t rollback_fn)
{
	struct sto_pipeline_action *action;

	action = pipeline_action_alloc(type, action_fn, rollback_fn);
	if (spdk_unlikely(!action)) {
		return -ENOMEM;
	}

	TAILQ_INSERT_TAIL(&pipe->action_queue, action, list);

	return 0;
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
sto_pipeline_add_step(struct sto_pipeline *pipe, const struct sto_pipeline_step *step)
{
	static enum sto_pipeline_action_type action_map[STO_PL_STEP_CNT] = {
		[STO_PL_STEP_SINGLE] = STO_PL_ACTION_NORMAL,
		[STO_PL_STEP_CONSTRUCTOR] = STO_PL_ACTION_CONSTRUCTOR,
		[STO_PL_STEP_TERMINATOR] = STO_PL_ACTION_CNT,
	};

	assert(step->type <= SPDK_COUNTOF(action_map));

	return sto_pipeline_add_action(pipe, action_map[step->type], step->action_fn, step->rollback_fn);
}

int
sto_pipeline_add_steps(struct sto_pipeline *pipe, const struct sto_pipeline_step *steps)
{
	const struct sto_pipeline_step *step;
	int rc;

	for (step = steps; step && step->type != STO_PL_STEP_TERMINATOR; step++) {
		rc = sto_pipeline_add_step(pipe, step);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to add step\n");
			return rc;
		}
	}

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
	int rc = 0;

	if (spdk_unlikely(pipe->error)) {
		goto finish;
	}

	action = pipeline_next_action(pipe);
	if (!action) {
		goto finish;
	}

	rc = action->execute(action, pipe);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to action for pipeline[%p]\n", pipe);

		pipe->error = rc;
		goto finish;
	}

	return;

finish:
	pipeline_action_finish(pipe);

	return;
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
