#include <spdk/queue.h>
#include <spdk/string.h>
#include <spdk/likely.h>
#include <spdk/log.h>

#include "sto_subsystem.h"
#include "sto_lib.h"
#include "err.h"

static TAILQ_HEAD(sto_subsystem_list, sto_subsystem) g_sto_subsystems
	= TAILQ_HEAD_INITIALIZER(g_sto_subsystems);

void
sto_add_subsystem(struct sto_subsystem *subsystem)
{
	TAILQ_INSERT_TAIL(&g_sto_subsystems, subsystem, list);
}

static struct sto_subsystem *
_subsystem_find(struct sto_subsystem_list *list, const char *name)
{
	struct sto_subsystem *iter;

	TAILQ_FOREACH(iter, list, list) {
		if (!strcmp(name, iter->name)) {
			return iter;
		}
	}

	return NULL;
}

struct sto_subsystem *
sto_subsystem_find(const char *name)
{
	return _subsystem_find(&g_sto_subsystems, name);
}

static int
sto_subsystem_decode_params(struct sto_req *req, const struct spdk_json_val *params)
{
	const struct spdk_json_val *cdb;
	struct sto_req_ops *ops = req->ops;
	int rc = 0;

	cdb = sto_json_decode_next_object(params);
	if (IS_ERR(cdb)) {
		SPDK_ERRLOG("Failed to decode CDB for req[%p]\n", req);
		return PTR_ERR(cdb);
	}

	rc = ops->decode_cdb(req, cdb);
	if (rc) {
		SPDK_ERRLOG("Failed to parse CDB for req[%p], rc=%d\n", req, rc);
		goto out;
	}

out:
	free((struct spdk_json_val *) cdb);

	return rc;
}

static const struct sto_ops *
sto_subsystem_find_ops(struct sto_subsystem *subsystem, const char *op_name)
{
	const struct sto_op_table *op_table;
	int i;

	op_table = subsystem->op_table;
	assert(op_table);

	for (i = 0; i < op_table->size; i++) {
		const struct sto_ops *op = &op_table->ops[i];

		if (!strcmp(op_name, op->name)) {
			return op;
		}
	}

	return NULL;
}

static const struct sto_ops *
sto_subsystem_get_cdbops(struct sto_subsystem *subsystem,
			 const struct spdk_json_val *params)
{
	char *op_name = NULL;
	const struct sto_ops *op;
	int rc = 0;

	rc = sto_json_decode_object_str(params, "op", &op_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode op, rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	op = sto_subsystem_find_ops(subsystem, op_name);
	if (!op) {
		SPDK_ERRLOG("Failed to find op %s\n", op_name);
		free(op_name);
		return ERR_PTR(-EINVAL);
	}

	free(op_name);

	return op;
}

struct sto_context *
sto_subsystem_parse(struct sto_subsystem *subsystem, const struct spdk_json_val *params)
{
	const struct sto_ops *op;
	struct sto_req *req = NULL;
	int rc;

	op = sto_subsystem_get_cdbops(subsystem, params);
	if (IS_ERR(op)) {
		SPDK_ERRLOG("Failed to decode params\n");
		return NULL;
	}

	req = op->req_constructor(op);
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to construct req\n");
		return NULL;
	}

	rc = sto_subsystem_decode_params(req, params);
	if (rc) {
		struct sto_req_ops *ops = req->ops;
		SPDK_ERRLOG("Failed to decode params, rc=%d\n", rc);

		ops->free(req);
		return NULL;
	}

	return &req->ctx;
}
