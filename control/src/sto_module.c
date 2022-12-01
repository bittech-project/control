#include <spdk/thread.h>
#include <spdk/string.h>
#include <spdk/likely.h>
#include <spdk/log.h>

#include "sto_module.h"
#include "err.h"

#define STO_MODULE_POLL_PERIOD	1000 /* 1ms */

static struct spdk_poller *g_sto_module_poller;

static TAILQ_HEAD(sto_module_req_list, sto_module_req) g_sto_module_req_list
	= TAILQ_HEAD_INITIALIZER(g_sto_module_req_list);

static TAILQ_HEAD(sto_module_list, sto_module) g_sto_modules
	= TAILQ_HEAD_INITIALIZER(g_sto_modules);


static void
sto_module_req_real_exec(struct sto_module_req *module_req)
{
	struct sto_req *req;
	sto_module_tt *tt;
	sto_module_transition_t transition;
	int rc = 0;

	req = &module_req->req;
	tt = module_req->tt;
	transition = tt->transitions[module_req->phase];

	if (spdk_unlikely(module_req->returncode)) {
		rc = module_req->returncode;
		goto out_err;
	}

	if (module_req->phase == tt->transition_num) {
		goto out_response;
	}

	rc = transition(module_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to exec %d transition\n",
			    module_req->phase);
		goto out_err;
	}

	module_req->phase++;

	return;

out_err:
	sto_err(req->ctx.err_ctx, rc);

out_response:
	sto_req_response(req);

	return;
}

static int
sto_module_poll(void *ctx)
{
	struct sto_module_req_list module_req_list = TAILQ_HEAD_INITIALIZER(module_req_list);
	struct sto_module_req *module_req, *tmp;

	if (TAILQ_EMPTY(&g_sto_module_req_list)) {
		return SPDK_POLLER_IDLE;
	}

	TAILQ_SWAP(&module_req_list, &g_sto_module_req_list, sto_module_req, list);

	TAILQ_FOREACH_SAFE(module_req, &module_req_list, list, tmp) {
		TAILQ_REMOVE(&module_req_list, module_req, list);

		sto_module_req_real_exec(module_req);
	}

	return SPDK_POLLER_BUSY;
}

void
sto_add_module(struct sto_module *module)
{
	TAILQ_INSERT_TAIL(&g_sto_modules, module, list);
}

static struct sto_module *
_module_find(struct sto_module_list *list, const char *name)
{
	struct sto_module *module;

	TAILQ_FOREACH(module, list, list) {
		if (!strcmp(name, module->name)) {
			return module;
		}
	}

	return NULL;
}

struct sto_module *
sto_module_find(const char *name)
{
	return _module_find(&g_sto_modules, name);
}

static const struct sto_ops *
sto_module_find_ops(struct sto_module *module, const char *op_name)
{
	const struct sto_op_table *op_table;
	int i;

	op_table = module->op_table;
	assert(op_table);

	for (i = 0; i < op_table->size; i++) {
		const struct sto_ops *op = &op_table->ops[i];

		if (!strcmp(op_name, op->name)) {
			return op;
		}
	}

	return NULL;
}

static struct sto_module *
sto_module_parse(const struct spdk_json_val *params)
{
	struct sto_module *module;
	char *module_name = NULL;
	int rc = 0;

	rc = sto_json_decode_object_str(params, "module", &module_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode module for rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	module = sto_module_find(module_name);

	free(module_name);

	if (spdk_unlikely(!module)) {
		SPDK_ERRLOG("Failed to find module\n");
		return ERR_PTR(-EINVAL);
	}

	return module;
}

static const struct sto_ops *
sto_module_parse_ops(struct sto_module *module, const struct spdk_json_val *params)
{
	char *op_name = NULL;
	const struct sto_ops *op;
	int rc = 0;

	rc = sto_json_decode_object_str(params, "op", &op_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode op, rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	op = sto_module_find_ops(module, op_name);
	if (!op) {
		SPDK_ERRLOG("Failed to find op %s\n", op_name);
		free(op_name);
		return ERR_PTR(-EINVAL);
	}

	free(op_name);

	return op;
}

static const struct sto_ops *
sto_module_decode_ops(const struct spdk_json_val *params,
		      const struct spdk_json_val **params_cdb)
{
	struct sto_module *module;
	const struct spdk_json_val *op_cdb;
	const struct sto_ops *op;

	if (*params_cdb) {
		SPDK_ERRLOG("Params CDB is already set\n");
		return ERR_PTR(-EINVAL);
	}

	module = sto_module_parse(params);
	if (IS_ERR(module)) {
		SPDK_ERRLOG("Failed to parse module\n");
		return ERR_CAST(module);
	}

	op_cdb = sto_json_next_object(params);
	if (IS_ERR_OR_NULL(op_cdb)) {
		SPDK_ERRLOG("Failed to decode next JSON object\n");
		return op_cdb ? ERR_CAST(op_cdb) : ERR_PTR(-EINVAL);
	}

	op = sto_module_parse_ops(module, op_cdb);

	*params_cdb = sto_json_next_object_and_free(op_cdb);
	if (IS_ERR(*params_cdb)) {
		SPDK_ERRLOG("Failed to decode next JSON object\n");
		return ERR_CAST(*params_cdb);
	}

	return op;
}

void
sto_module_req_process(struct sto_module_req *module_req)
{
	TAILQ_INSERT_TAIL(&g_sto_module_req_list, module_req, list);
}

STO_REQ_CONSTRUCTOR_DEFINE(module)

static int
sto_module_req_decode_cdb(struct sto_req *req, const struct spdk_json_val *cdb)
{
	struct sto_module_req *module_req = STO_REQ_TYPE(req, module);
	struct sto_module_req_params_constructor *constructor = req->params_constructor;
	int rc = 0;

	if (cdb) {
		module_req->cdb = sto_json_copy_object(cdb);
		if (IS_ERR(module_req->cdb)) {
			SPDK_ERRLOG("Failed to copy CDB for module req\n");
			rc = PTR_ERR(module_req->cdb);
			module_req->cdb = NULL;

			return rc;
		}
	}

	module_req->tt = constructor->tt;

	assert(module_req->tt->transition_num);

	return rc;
}

static int
sto_module_req_exec(struct sto_req *req)
{
	struct sto_module_req *module_req = STO_REQ_TYPE(req, module);

	sto_module_req_process(module_req);

	return 0;
}

static void
sto_module_req_free(struct sto_req *req)
{
	struct sto_module_req *module_req = STO_REQ_TYPE(req, module);

	free((struct spdk_json_val *) module_req->cdb);

	rte_free(module_req);
}

struct sto_req_ops sto_module_req_ops = {
	.decode_cdb = sto_module_req_decode_cdb,
	.exec = sto_module_req_exec,
	.end_response = sto_dummy_req_end_response,
	.free = sto_module_req_free,
};

int
sto_module_init(void)
{
	g_sto_module_poller = SPDK_POLLER_REGISTER(sto_module_poll, NULL, STO_MODULE_POLL_PERIOD);
	if (spdk_unlikely(!g_sto_module_poller)) {
		SPDK_ERRLOG("Cann't register the STO req poller\n");
		return -ENOMEM;
	}

	return 0;
}

void
sto_module_fini(void)
{
	spdk_poller_unregister(&g_sto_module_poller);
}

static struct sto_core_component g_module_component =
	STO_CORE_COMPONENT_INITIALIZER("module", sto_module_init, sto_module_fini, sto_module_decode_ops);
STO_CORE_COMPONENT_REGISTER(g_module_component)
