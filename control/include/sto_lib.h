#ifndef _STO_LIB_H_
#define _STO_LIB_H_

#include <spdk/util.h>

#include "sto_utils.h"

#include "sto_subsystem.h"
#include "sto_rpc_readdir.h"
#include "sto_tree.h"

typedef void *(*sto_params_alloc)(void);
typedef void (*sto_params_free)(void *params);

typedef int (*sto_params_parse)(void *priv, void *params);

struct sto_decoder {
	const struct spdk_json_object_decoder *decoders;
	size_t num_decoders;

	sto_params_alloc params_alloc;
	sto_params_free params_free;

	bool allow_empty;
	bool initialized;
};
#define STO_DECODER_INITIALIZER(decoders, params_alloc, params_free)	\
	{decoders, SPDK_COUNTOF(decoders), params_alloc, params_free, false, true}

#define STO_DECODER_INITIALIZER_EMPTY(decoders, params_alloc, params_free)	\
	{decoders, SPDK_COUNTOF(decoders), params_alloc, params_free, true, true}

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

struct sto_ops;

typedef struct sto_req *(*sto_req_constructor_t)(const struct sto_ops *op);

struct sto_ops {
	const char *name;

	sto_req_constructor_t req_constructor;
	struct sto_req_ops *req_ops;

	void *params_constructor;
};

struct sto_op_table {
	const struct sto_ops *ops;
	size_t size;
};
#define STO_OP_TABLE_INITIALIZER(ops) {ops, SPDK_COUNTOF(ops)}

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
sto_req_init(struct sto_req *req, const struct sto_ops *op)
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

struct sto_write_req_params {
	const char *file;
	char *data;
};

struct sto_write_req {
	struct sto_req req;

	struct sto_write_req_params params;
};

struct sto_write_req_params_constructor {
	struct sto_decoder decoder;

	const char *(*file_path)(void *params);
	char *(*data)(void *params);

	struct {
		struct sto_write_req_params *params;
	} inner;
};

extern struct sto_req_ops sto_write_req_ops;

static inline struct sto_write_req *
sto_write_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct sto_write_req, req);
}

struct sto_req *sto_write_req_constructor(const struct sto_ops *op);

struct sto_read_req_params {
	const char *file;
	uint32_t size;
};

struct sto_read_req {
	struct sto_req req;

	struct sto_read_req_params params;
	char *buf;
};

struct sto_read_req_params_constructor {
	struct sto_decoder decoder;

	const char *(*file_path)(void *params);
	uint32_t (*size)(void *params);

	struct {
		struct sto_read_req_params *params;
	} inner;
};

extern struct sto_req_ops sto_read_req_ops;

static inline struct sto_read_req *
sto_read_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct sto_read_req, req);
}

struct sto_req *sto_read_req_constructor(const struct sto_ops *op);

struct sto_readlink_req_params {
	const char *file;
};

struct sto_readlink_req {
	struct sto_req req;

	struct sto_readlink_req_params params;
	char *buf;
};

struct sto_readlink_req_params_constructor {
	struct sto_decoder decoder;

	const char *(*file_path)(void *params);

	struct {
		struct sto_readlink_req_params *params;
	} inner;
};

extern struct sto_req_ops sto_readlink_req_ops;

static inline struct sto_readlink_req *
sto_readlink_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct sto_readlink_req, req);
}

struct sto_req *sto_readlink_req_constructor(const struct sto_ops *op);

struct sto_readdir_req_params {
	const char *name;
	char *dirpath;
#define EXCLUDE_LIST_MAX 20
	const char *exclude_list[EXCLUDE_LIST_MAX];
};

struct sto_readdir_req {
	struct sto_req req;

	struct sto_readdir_req_params params;
	struct sto_dirents dirents;
};

struct sto_readdir_req_params_constructor {
	struct sto_decoder decoder;

	const char *(*name)(void *params);
	char *(*dirpath)(void *params);
	int (*exclude)(const char **arr);

	struct {
		struct sto_readdir_req_params *params;
	} inner;
};

extern struct sto_req_ops sto_readdir_req_ops;

static inline struct sto_readdir_req *
sto_readdir_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct sto_readdir_req, req);
}

struct sto_req *sto_readdir_req_constructor(const struct sto_ops *op);

struct sto_tree_req_params {
	char *dirpath;
	uint32_t depth;
	bool only_dirs;

	sto_tree_info_json_t info_json;
};

struct sto_tree_req {
	struct sto_req req;

	struct sto_tree_req_params params;
	struct sto_tree_info info;
};

struct sto_tree_req_params_constructor {
	struct sto_decoder decoder;

	char *(*dirpath)(void *params);
	uint32_t (*depth)(void *params);
	bool (*only_dirs)(void *params);

	sto_tree_info_json_t info_json;

	struct {
		struct sto_tree_req_params *params;
	} inner;
};

extern struct sto_req_ops sto_tree_req_ops;

static inline struct sto_tree_req *
sto_tree_req(struct sto_req *req)
{
	return SPDK_CONTAINEROF(req, struct sto_tree_req, req);
}

struct sto_req *sto_tree_req_constructor(const struct sto_ops *op);

int sto_decoder_parse(struct sto_decoder *decoder, const struct spdk_json_val *data,
		      sto_params_parse params_parse, void *priv);

void sto_err(struct sto_err_context *err, int rc);

void sto_status_ok(struct spdk_json_write_ctx *w);
void sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err);

#endif /* _STO_LIB_H_ */
