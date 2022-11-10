#ifndef _STO_LIB_H_
#define _STO_LIB_H_

#include <spdk/util.h>

#include "sto_subsystem.h"
#include "sto_readdir_front.h"
#include "sto_tree.h"

typedef void *(*sto_params_alloc)(void);
typedef void (*sto_params_free)(void *params);

typedef int (*sto_params_parse)(void *priv, void *params);

struct sto_decoder {
	const struct spdk_json_object_decoder *decoders;
	size_t num_decoders;

	sto_params_alloc params_alloc;
	sto_params_free params_free;

	bool initialized;
};
#define STO_DECODER_INITIALIZER(decoders, params_alloc, params_free)	\
	{decoders, SPDK_COUNTOF(decoders), params_alloc, params_free, true}

struct sto_err_context {
	int rc;
	const char *errno_msg;
};

struct sto_context {
	void *priv;
	sto_subsys_response_t response;
	struct sto_err_context *err_ctx;
};

struct sto_req;
struct sto_cdbops;

typedef int (*sto_req_decode_cdb_t)(struct sto_req *req, const struct spdk_json_val *cdb);
typedef int (*sto_req_exec_t)(struct sto_req *req);
typedef void (*sto_req_end_response_t)(struct sto_req *req, struct spdk_json_write_ctx *w);
typedef void (*sto_req_free_t)(struct sto_req *req);

struct sto_req_ops {
	sto_req_decode_cdb_t decode_cdb;
	sto_req_exec_t exec;
	sto_req_end_response_t end_response;
	sto_req_free_t free;
};

typedef struct sto_req *(*sto_req_constructor_t)(const struct sto_cdbops *op);

struct sto_cdbops {
	const char *name;

	sto_req_constructor_t req_constructor;

	struct sto_req_ops *req_ops;
	void *params_constructor;
};

struct sto_req {
	struct sto_context ctx;

	struct sto_req_ops *ops;
	void *params_constructor;
};

static inline struct sto_req *
sto_req(struct sto_context *ctx)
{
	return SPDK_CONTAINEROF(ctx, struct sto_req, ctx);
}

static inline void
sto_req_init(struct sto_req *req, const struct sto_cdbops *op)
{
	req->ops = op->req_ops;
	req->params_constructor = op->params_constructor;
}

static inline void
sto_req_response(struct sto_req *req)
{
	struct sto_context *ctx = &req->ctx;

	ctx->response(ctx->priv);
}

struct sto_write_req {
	struct sto_req req;

	const char *file;
	char *data;
};

struct sto_write_req_params {
	struct sto_decoder decoder;

	struct {
		const char *(*file_path)(void *params);
		char *(*data)(void *params);
	} constructor;

	struct {
		struct sto_write_req *req;
	} inner;
};

extern struct sto_req_ops sto_write_req_ops;

static inline struct sto_write_req *
sto_write_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct sto_write_req, req);
}

struct sto_req *sto_write_req_constructor(const struct sto_cdbops *op);

struct sto_ls_req {
	struct sto_req req;

	const char *name;
	char *dirpath;
#define EXCLUDE_LIST_MAX 20
	const char *exclude_list[EXCLUDE_LIST_MAX];

	struct sto_readdir_result result;
};

struct sto_ls_req_params {
	struct sto_decoder decoder;

	struct {
		const char *(*name)(void *params);
		char *(*dirpath)(void *params);
		int (*exclude)(const char **arr);
	} constructor;

	struct {
		struct sto_ls_req *req;
	} inner;
};

extern struct sto_req_ops sto_ls_req_ops;

static inline struct sto_ls_req *
sto_ls_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct sto_ls_req, req);
}

struct sto_req *sto_ls_req_constructor(const struct sto_cdbops *op);

struct sto_tree_req {
	struct sto_req req;

	char *dirpath;
	uint32_t depth;

	struct sto_tree_info info;
};

struct sto_tree_req_params {
	struct sto_decoder decoder;

	struct {
		char *(*dirpath)(void *params);
		uint32_t (*depth)(void *params);
	} constructor;

	struct {
		struct sto_tree_req *req;
	} inner;
};

extern struct sto_req_ops sto_tree_req_ops;

static inline struct sto_tree_req *
sto_tree_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct sto_tree_req, req);
}

struct sto_req *sto_tree_req_constructor(const struct sto_cdbops *op);

int sto_decoder_parse(struct sto_decoder *decoder, const struct spdk_json_val *data,
		      sto_params_parse params_parse, void *priv);

int sto_decode_object_str(const struct spdk_json_val *values,
			  const char *name, char **value);
const struct spdk_json_val *sto_decode_next_cdb(const struct spdk_json_val *params);

void sto_err(struct sto_err_context *err, int rc);

void sto_status_ok(struct spdk_json_write_ctx *w);
void sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err);

#endif /* _STO_LIB_H_ */
