#ifndef _STO_LIB_H_
#define _STO_LIB_H_

#include <spdk/queue.h>
#include <spdk/util.h>
#include <spdk/likely.h>

#include <rte_malloc.h>

#include "sto_utils.h"

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

int sto_decoder_parse(struct sto_decoder *decoder, const struct spdk_json_val *data,
		      sto_params_parse params_parse, void *priv);

struct sto_err_context {
	int rc;
	const char *errno_msg;
};

typedef void (*sto_context_done_t)(void *priv);

struct sto_context {
	void *priv;
	sto_context_done_t done;
	struct sto_err_context *err_ctx;
};

void sto_err(struct sto_err_context *err, int rc);

void sto_status_ok(struct spdk_json_write_ctx *w);
void sto_status_failed(struct spdk_json_write_ctx *w, struct sto_err_context *err);

struct sto_req;

typedef int (*sto_req_decode_cdb_t)(struct sto_req *req, void *params_constructor,
				    const struct spdk_json_val *cdb);
typedef int (*sto_req_exec_constructor_t)(struct sto_req *req, int state);
typedef void (*sto_req_response_t)(struct sto_req *req, struct spdk_json_write_ctx *w);

struct sto_req_ops {
	sto_req_decode_cdb_t decode_cdb;
	sto_req_exec_constructor_t exec_constructor;
	sto_req_response_t response;
};

typedef void (*sto_req_priv_deinit_t)(void *priv);

struct sto_req_properties {
	uint32_t priv_size;
	sto_req_priv_deinit_t priv_deinit;

	struct sto_req_ops ops;
};

struct sto_ops {
	const char *name;
	void *params_constructor;
	const struct sto_req_properties *req_properties;
};

struct sto_op_table {
	const struct sto_ops *ops;
	size_t size;
};
#define STO_OP_TABLE_INITIALIZER(ops) {ops, SPDK_COUNTOF(ops)}

int sto_lib_init(void);
void sto_lib_fini(void);

typedef int (*sto_req_exec_t)(struct sto_req *req);

struct sto_exec_entry {
	sto_req_exec_t exec_fn;
	sto_req_exec_t rollback_fn;
};

struct sto_exec_ctx {
	sto_req_exec_t exec_fn;

	TAILQ_ENTRY(sto_exec_ctx) list;
};

TAILQ_HEAD(sto_exec_list, sto_exec_ctx);

struct sto_req {
	struct sto_context ctx;

	void *priv;
	sto_req_priv_deinit_t priv_deinit;

	const struct sto_req_ops *ops;

	int returncode;
	int state;

	TAILQ_ENTRY(sto_req) list;

	struct sto_exec_list exe_queue;
	struct sto_exec_list rollback_stack;
};

#define STO_REQ(x) \
	SPDK_CONTAINEROF((x), struct sto_req, ctx)

void sto_req_process(struct sto_req *req, int rc);

static inline void
sto_req_process_start(struct sto_req *req)
{
	sto_req_process(req, 0);
}

int sto_req_add_exec(struct sto_req *req, sto_req_exec_t exec_fn, sto_req_exec_t rollback_fn);
int sto_req_add_exec_entries(struct sto_req *req, const struct sto_exec_entry *entries, size_t size);
#define STO_REQ_ADD_EXEC_ENTRIES(req, entries) \
	sto_req_add_exec_entries((req), (entries), SPDK_COUNTOF((entries)))

static inline void
sto_req_exec_done(void *priv, int rc)
{
	struct sto_req *req = priv;

	sto_req_process(req, rc);
}

static inline void
sto_req_done(struct sto_req *req)
{
	struct sto_context *ctx = &req->ctx;

	ctx->done(ctx->priv);
}

struct sto_req *sto_req_alloc(const struct sto_req_properties *properties);
void sto_req_free(struct sto_req *req);

static inline int
sto_dummy_req_decode_cdb(struct sto_req *req, void *params_constructor, const struct spdk_json_val *cdb)
{
	return 0;
}

static inline int
sto_dummy_req_exec_constructor(struct sto_req *req, int state)
{
	return 0;
}

static inline void
sto_dummy_req_response(struct sto_req *req, struct spdk_json_write_ctx *w)
{
	sto_status_ok(w);
}

struct sto_write_req_params {
	const char *file;
	char *data;
};

struct sto_write_req_params_constructor {
	struct sto_decoder decoder;

	const char *(*file_path)(void *params);
	char *(*data)(void *params);

	struct {
		struct sto_write_req_params *params;
	} inner;
};

struct sto_write_req_info {
	struct sto_write_req_params params;
};

extern const struct sto_req_properties sto_write_req_properties;

struct sto_read_req_params {
	const char *file;
	uint32_t size;
};

struct sto_read_req_params_constructor {
	struct sto_decoder decoder;

	const char *(*file_path)(void *params);
	uint32_t (*size)(void *params);

	struct {
		struct sto_read_req_params *params;
	} inner;
};

struct sto_read_req_info {
	struct sto_read_req_params params;
	char *buf;
};

extern const struct sto_req_properties sto_read_req_properties;

struct sto_readlink_req_params {
	const char *file;
};

struct sto_readlink_req_params_constructor {
	struct sto_decoder decoder;

	const char *(*file_path)(void *params);

	struct {
		struct sto_readlink_req_params *params;
	} inner;
};

struct sto_readlink_req_info {
	struct sto_req req;

	struct sto_readlink_req_params params;
	char *buf;
};

extern const struct sto_req_properties sto_readlink_req_properties;

struct sto_readdir_req_params {
	const char *name;
	char *dirpath;
#define EXCLUDE_LIST_MAX 20
	const char *exclude_list[EXCLUDE_LIST_MAX];
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

struct sto_readdir_req_info {
	struct sto_readdir_req_params params;
	struct sto_dirents dirents;
};

extern const struct sto_req_properties sto_readdir_req_properties;

struct sto_tree_req_params {
	char *dirpath;
	uint32_t depth;
	bool only_dirs;

	sto_tree_info_json_t info_json;
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

struct sto_tree_req_info {
	struct sto_tree_req_params params;
	struct sto_tree_node tree_root;
};

extern const struct sto_req_properties sto_tree_req_properties;

#endif /* _STO_LIB_H_ */
