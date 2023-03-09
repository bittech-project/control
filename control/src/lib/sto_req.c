#include "sto_req.h"

#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>

#include "sto_err.h"
#include "sto_core.h"
#include "sto_lib.h"
#include "sto_pipeline.h"

struct sto_json_iter;

static struct sto_pipeline_engine *g_req_engine;

static void sto_req_type_deinit(struct sto_req_type *type);

static int
sto_req_type_init(struct sto_req_type *type, const struct sto_req_properties *properties)
{
	int rc = 0;

	if (properties->params_size) {
		type->params = calloc(1, properties->params_size);
		if (spdk_unlikely(!type->params)) {
			SPDK_ERRLOG("Failed to alloc req type params\n");
			rc = -ENOMEM;
			goto error;
		}

		type->params_deinit_fn = properties->params_deinit_fn;
	}

	if (properties->priv_size) {
		type->priv = calloc(1, properties->priv_size);
		if (spdk_unlikely(!type->priv)) {
			SPDK_ERRLOG("Failed to alloc req type priv\n");
			rc = -ENOMEM;
			goto error;
		}

		type->priv_deinit_fn = properties->priv_deinit_fn;
	}

	type->response = properties->response;

	return 0;

error:
	sto_req_type_deinit(type);

	return rc;
}

static void
sto_req_type_deinit(struct sto_req_type *type)
{
	if (type->params) {
		if (type->params_deinit_fn) {
			type->params_deinit_fn(type->params);
		}

		free(type->params);
	}

	if (type->priv) {
		if (type->priv_deinit_fn) {
			type->priv_deinit_fn(type->priv);
		}

		free(type->priv);
	}
}

int
sto_req_type_parse_params(struct sto_req_type *type,
			  const struct sto_ops_params_properties *properties,
			  const struct sto_json_iter *iter,
			  sto_ops_req_params_constructor_t req_params_constructor)
{
	void *ops_params = NULL;
	int rc = 0;

	assert(!properties || req_params_constructor);

	if (!req_params_constructor) {
		return 0;
	}

	if (properties) {
		ops_params = sto_ops_params_decode(properties, iter);
		if (IS_ERR(ops_params)) {
			SPDK_ERRLOG("Failed to parse ops params\n");
			return PTR_ERR(ops_params);
		}
	}

	rc = req_params_constructor(type->params, properties ? ops_params : (void *) iter);

	if (properties) {
		sto_ops_params_free(properties, ops_params);
	}

	return rc;
}

struct sto_req *
sto_req_alloc(const struct sto_req_properties *properties)
{
	struct sto_req *req;
	int rc;

	req = calloc(1, sizeof(*req));
	if (spdk_unlikely(!req)) {
		SPDK_ERRLOG("Failed to alloc STO req\n");
		return NULL;
	}

	rc = sto_req_type_init(&req->type, properties);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init STO req\n");
		goto free_req;
	}

	rc = sto_pipeline_init(&req->pipeline, NULL);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to init req pipeline\n");
		goto free_req;
	}

	rc = sto_pipeline_add_steps(&req->pipeline, properties->steps);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to add steps to pipeline\n");
		goto free_req;
	}

	sto_pipeline_set_priv(&req->pipeline, req);

	return req;

free_req:
	sto_req_free(req);

	return NULL;
}

void
sto_req_free(struct sto_req *req)
{
	sto_pipeline_deinit(&req->pipeline);
	sto_req_type_deinit(&req->type);

	free(req);
}

void
sto_req_run(struct sto_req *req)
{
	sto_pipeline_run(g_req_engine, &req->pipeline, sto_req_done, req);
}

static void
sto_req_core_done(struct sto_core_req *core_req)
{
	struct sto_req *req = core_req->priv;
	int rc = core_req->err_ctx.rc;

	sto_core_req_free(core_req);

	sto_pipeline_step_next(&req->pipeline, rc);
}

int
sto_req_core_submit(struct sto_req *req, sto_core_req_done_t done,
		    const struct sto_json_head_raw *head)
{
	return sto_core_process_raw(head, done ?: sto_req_core_done, req);
}

int
sto_req_lib_init(void)
{
	g_req_engine = sto_pipeline_engine_create("STO req");
	if (spdk_unlikely(!g_req_engine)) {
		SPDK_ERRLOG("Cann't create the STO req engine\n");
		return -ENOMEM;
	}

	return 0;
}

void
sto_req_lib_fini(void)
{
	sto_pipeline_engine_destroy(g_req_engine);
}
