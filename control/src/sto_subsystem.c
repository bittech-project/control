#include <spdk/json.h>
#include <spdk/string.h>
#include <spdk/likely.h>
#include <spdk/log.h>

#include "sto_subsystem.h"
#include "sto_core.h"
#include "sto_err.h"

static TAILQ_HEAD(sto_subsystem_list, sto_subsystem) g_sto_subsystems =
	TAILQ_HEAD_INITIALIZER(g_sto_subsystems);

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

static struct spdk_json_write_ctx *
sto_subsystem_json_begin(const char *subsystem, const char *op, struct sto_json_ctx *ctx)
{
	struct spdk_json_write_ctx *w;

	w = spdk_json_write_begin(sto_json_ctx_write_cb, ctx, 0);
	if (w == NULL) {
		return NULL;
	}

	spdk_json_write_object_begin(w);
	spdk_json_write_named_string(w, "subsystem", subsystem);
	spdk_json_write_named_string(w, "op", op);

	return w;
}

static void
sto_subsystem_json_end(struct spdk_json_write_ctx *w)
{
	assert(w != NULL);

	spdk_json_write_object_end(w);
	spdk_json_write_end(w);
}

static struct sto_json_ctx *
sto_subsystem_dump_req(const char *subsystem, const char *op,
		       void *params, sto_subsystem_dump_params_t dump_params)
{
	struct sto_json_ctx *ctx;
	struct spdk_json_write_ctx *w;

	ctx = sto_json_ctx_alloc();
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc json context\n");
		return NULL;
	}

	w = sto_subsystem_json_begin(subsystem, op, ctx);
	if (spdk_unlikely(!w)) {
		SPDK_ERRLOG("Failed to begin subsystem json\n");
		goto free_ctx;
	}

	if (dump_params) {
		dump_params(params, w);
	}

	sto_subsystem_json_end(w);

	return ctx;

free_ctx:
	sto_json_ctx_free(ctx);

	return NULL;
}

struct sto_subsystem_send_ctx {
	void *priv;
	sto_subsystem_send_done_t done;

	struct sto_json_ctx *json_ctx;
};

static void
sto_subsystem_send_ctx_init(struct sto_subsystem_send_ctx *ctx,
			    struct sto_subsystem_args *args, struct sto_json_ctx *json_ctx)
{
	ctx->priv = args->priv;
	ctx->done = args->done;
	ctx->json_ctx = json_ctx;
}

static void
sto_subsystem_send_ctx_free(struct sto_subsystem_send_ctx *ctx)
{
	sto_json_ctx_free(ctx->json_ctx);
	rte_free(ctx);
}

void
sto_subsystem_send_done(struct sto_core_req *core_req)
{
	struct sto_subsystem_send_ctx *ctx = core_req->priv;
	void *priv;
	sto_subsystem_send_done_t done;

	priv = ctx->priv;
	done = ctx->done;

	sto_subsystem_send_ctx_free(ctx);

	done(priv, core_req);
}

int
sto_subsystem_send(const char *subsystem, const char *op,
		   void *params, sto_subsystem_dump_params_t dump_params,
		   struct sto_subsystem_args *args)
{
	struct sto_subsystem_send_ctx *ctx;
	struct sto_json_ctx *json_ctx;
	struct sto_core_args core_args;
	int rc = 0;

	ctx = rte_zmalloc(NULL, sizeof(*ctx), 0);
	if (spdk_unlikely(!ctx)) {
		SPDK_ERRLOG("Failed to alloc subsystemd send context\n");
		return -ENOMEM;
	}

	json_ctx = sto_subsystem_dump_req(subsystem, op, params, dump_params);
	if (spdk_unlikely(!json_ctx)) {
		SPDK_ERRLOG("Failed to dump target_add scst subsystem req\n");
		rc = -ENOMEM;
		goto free_ctx;
	}

	sto_subsystem_send_ctx_init(ctx, args, json_ctx);

	core_args.priv = ctx;
	core_args.done = sto_subsystem_send_done;

	rc = sto_core_process_start(json_ctx->values, &core_args);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to start core process\n");
		goto free_json_ctx;
	}

	return 0;

free_json_ctx:
	sto_json_ctx_free(ctx->json_ctx);

free_ctx:
	rte_free(ctx);

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

static struct sto_subsystem *
sto_subsystem_parse(const struct spdk_json_val *params)
{
	struct sto_subsystem *subsystem;
	char *subsystem_name = NULL;
	int rc = 0;

	rc = sto_json_decode_object_str(params, "subsystem", &subsystem_name);
	if (rc) {
		SPDK_ERRLOG("Failed to decode subsystem for rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	subsystem = sto_subsystem_find(subsystem_name);

	free(subsystem_name);

	if (spdk_unlikely(!subsystem)) {
		SPDK_ERRLOG("Failed to find subsystem\n");
		return ERR_PTR(-EINVAL);
	}

	return subsystem;
}

static const struct sto_ops *
sto_subsystem_parse_ops(struct sto_subsystem *subsystem, const struct spdk_json_val *params)
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

static const struct sto_ops *
sto_subsystem_decode_ops(const struct spdk_json_val *params,
			 const struct spdk_json_val **params_cdb)
{
	struct sto_subsystem *subsystem;
	const struct spdk_json_val *op_cdb;
	const struct sto_ops *op;

	if (*params_cdb) {
		SPDK_ERRLOG("Params CDB is already set\n");
		return ERR_PTR(-EINVAL);
	}

	subsystem = sto_subsystem_parse(params);
	if (IS_ERR(subsystem)) {
		SPDK_ERRLOG("Failed to parse subsystem\n");
		return ERR_CAST(subsystem);
	}

	op_cdb = sto_json_next_object(params);
	if (IS_ERR_OR_NULL(op_cdb)) {
		SPDK_ERRLOG("Failed to decode next JSON object\n");
		return op_cdb ? ERR_CAST(op_cdb) : ERR_PTR(-EINVAL);
	}

	op = sto_subsystem_parse_ops(subsystem, op_cdb);

	*params_cdb = sto_json_next_object_and_free(op_cdb);
	if (IS_ERR(*params_cdb)) {
		SPDK_ERRLOG("Failed to decode next JSON object\n");
		return ERR_CAST(*params_cdb);
	}

	return op;
}

static struct sto_core_component g_subsystem_component = STO_CORE_COMPONENT_INITIALIZER("subsystem", sto_subsystem_decode_ops);
STO_CORE_COMPONENT_REGISTER(g_subsystem_component)
