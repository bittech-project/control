#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_req.h"
#include "sto_err.h"

#define STO_REQ_ACTION_POLL_PERIOD	1000 /* 1ms */

static struct spdk_poller *g_sto_req_action_poller;

static TAILQ_HEAD(sto_req_list, sto_req) g_sto_req_list = TAILQ_HEAD_INITIALIZER(g_sto_req_list);


static int
sto_req_type_init(struct sto_req_type *type, const struct sto_req_properties *properties)
{
	if (properties->params_size) {
		type->params = rte_zmalloc(NULL, properties->params_size, 0);
		if (spdk_unlikely(!type->params)) {
			SPDK_ERRLOG("Failed to alloc req type params\n");
			return -ENOMEM;
		}

		type->params_deinit = properties->params_deinit;
	}

	if (properties->priv_size) {
		type->priv = rte_zmalloc(NULL, properties->priv_size, 0);
		if (spdk_unlikely(!type->priv)) {
			SPDK_ERRLOG("Failed to alloc req type priv\n");
			return -ENOMEM;
		}

		type->priv_deinit = properties->priv_deinit;
	}

	type->response = properties->response;

	return 0;
}

int
sto_req_type_parse_params(struct sto_req_type *type, const struct sto_ops_decoder *decoder,
			  const struct spdk_json_val *values,
			  sto_ops_req_params_constructor_t req_params_constructor)
{
	void *ops_params = NULL;
	int rc = 0;

	assert(!decoder || req_params_constructor);

	if (!req_params_constructor) {
		return 0;
	}

	if (decoder) {
		ops_params = sto_ops_decoder_params_parse(decoder, values);
		if (IS_ERR(ops_params)) {
			SPDK_ERRLOG("Failed to parse ops params\n");
			return PTR_ERR(ops_params);
		}
	}

	rc = req_params_constructor(type->params, ops_params);

	if (decoder) {
		sto_ops_decoder_params_free(decoder, ops_params);
	}

	return rc;
}

static void
sto_req_type_deinit(struct sto_req_type *type)
{
	if (type->params) {
		if (type->params_deinit) {
			type->params_deinit(type->params);
		}

		rte_free(type->params);
	}

	if (type->priv) {
		if (type->priv_deinit) {
			type->priv_deinit(type->priv);
		}

		rte_free(type->priv);
	}
}

struct sto_req *
sto_req_alloc(const struct sto_req_properties *properties)
{
	struct sto_req *req;
	int rc;

	req = rte_zmalloc(NULL, sizeof(*req), 0);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc STO req\n");
		return NULL;
	}

	TAILQ_INIT(&req->action_queue);
	TAILQ_INIT(&req->rollback_stack);

	rc = sto_req_type_init(&req->type, properties);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init STO req\n");
		goto free_req;
	}

	rc = sto_req_add_steps(req, properties->steps);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to add steps to req\n");
		goto free_req;
	}

	return req;

free_req:
	sto_req_free(req);

	return NULL;
}

static void sto_req_action_list_free(struct sto_req_action_list *actions);

void
sto_req_free(struct sto_req *req)
{
	sto_req_type_deinit(&req->type);

	sto_req_action_list_free(&req->action_queue);
	sto_req_action_list_free(&req->rollback_stack);

	rte_free(req);
}

static struct sto_req_action *
sto_req_action_alloc(sto_req_action_t action_fn)
{
	struct sto_req_action *action;

	action = rte_zmalloc(NULL, sizeof(*action), 0);
	if (spdk_unlikely(!action)) {
		SPDK_ERRLOG("Failed to alloc action\n");
		return NULL;
	}

	action->fn = action_fn;

	return action;
}

static void
sto_req_action_free(struct sto_req_action *action)
{
	rte_free(action);
}

static void
sto_req_action_list_free(struct sto_req_action_list *actions)
{
	struct sto_req_action *action, *tmp;

	TAILQ_FOREACH_SAFE(action, actions, list, tmp) {
		TAILQ_REMOVE(actions, action, list);

		sto_req_action_free(action);
	}
}

int
sto_req_add_raw_step(struct sto_req *req, sto_req_action_t action_fn, sto_req_action_t rollback_fn)
{
	struct sto_req_action *action;

	assert(action_fn);

	action = sto_req_action_alloc(action_fn);
	if (spdk_unlikely(!action)) {
		SPDK_ERRLOG("Failed to allocate action\n");
		return -ENOMEM;
	}

	TAILQ_INSERT_TAIL(&req->action_queue, action, list);

	if (rollback_fn) {
		struct sto_req_action *rollback;

		rollback = sto_req_action_alloc(rollback_fn);
		if (spdk_unlikely(!rollback)) {
			SPDK_ERRLOG("Failed to allocate rollback action\n");

			TAILQ_REMOVE(&req->action_queue, action, list);
			sto_req_action_free(action);

			return -ENOMEM;
		}

		TAILQ_INSERT_HEAD(&req->rollback_stack, rollback, list);
	}

	return 0;
}

int
sto_req_add_steps(struct sto_req *req, const struct sto_req_step *steps)
{
	const struct sto_req_step *step;
	int rc;

	for (step = steps; step && step->type != STO_REQ_STEP_TERMINATOR; step++) {
		rc = sto_req_add_step(req, step);
		if (spdk_unlikely(rc)) {
			SPDK_ERRLOG("Failed to add step\n");
			return rc;
		}
	}

	return 0;
}

static inline struct sto_req_action *
sto_req_next_action(struct sto_req *req)
{
	struct sto_req_action_list *actions;
	struct sto_req_action *action = NULL;

	if (spdk_likely(!req->rollback)) {
		actions = &req->action_queue;
	} else {
		actions = &req->rollback_stack;
	}

	if (!TAILQ_EMPTY(actions)) {
		action = TAILQ_FIRST(actions);
		TAILQ_REMOVE(actions, action, list);
	}

	return action;
}

static void
sto_req_action_finish(struct sto_req *req)
{
	if (!req->returncode || req->rollback) {
		goto done;
	}

	sto_err(req->ctx.err_ctx, req->returncode);
	req->returncode = 0;

	if (!TAILQ_EMPTY(&req->rollback_stack)) {
		req->rollback = true;
		sto_req_step_next(req, 0);
		return;
	}

done:
	sto_req_done(req);
}

static void
sto_req_action(struct sto_req *req)
{
	struct sto_req_action *action;
	int rc = 0;

	if (spdk_unlikely(req->returncode)) {
		goto finish;
	}

	action = sto_req_next_action(req);
	if (!action) {
		goto finish;
	}

	rc = action->fn(req);

	sto_req_action_free(action);

	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to action for req[%p]\n", req);

		req->returncode = rc;
		goto finish;
	}

	return;

finish:
	sto_req_action_finish(req);

	return;
}

static int
sto_req_action_poll(void *ctx)
{
	struct sto_req_list req_list = TAILQ_HEAD_INITIALIZER(req_list);
	struct sto_req *req, *tmp;

	if (TAILQ_EMPTY(&g_sto_req_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&req_list, &g_sto_req_list, sto_req, list);

	TAILQ_FOREACH_SAFE(req, &req_list, list, tmp) {
		TAILQ_REMOVE(&req_list, req, list);

		sto_req_action(req);
	}

	return SPDK_POLLER_BUSY;
}

void
sto_req_step_next(struct sto_req *req, int rc)
{
	req->returncode = rc;
	TAILQ_INSERT_TAIL(&g_sto_req_list, req, list);
}

int
sto_req_lib_init(void)
{
	g_sto_req_action_poller = SPDK_POLLER_REGISTER(sto_req_action_poll, NULL, STO_REQ_ACTION_POLL_PERIOD);
	if (spdk_unlikely(!g_sto_req_action_poller)) {
		SPDK_ERRLOG("Cann't register the STO req poller\n");
		return -ENOMEM;
	}

	return 0;
}

void
sto_req_lib_fini(void)
{
	spdk_poller_unregister(&g_sto_req_action_poller);
}
