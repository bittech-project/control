#ifndef _STO_REQ_H_
#define _STO_REQ_H_

#include <spdk/queue.h>
#include <spdk/util.h>

#include "sto_lib.h"
#include "sto_tree.h"

typedef void (*sto_req_context_done_t)(void *priv);

struct sto_req_context {
	void *priv;
	sto_req_context_done_t done;
	struct sto_err_context *err_ctx;
};

struct sto_req;

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

typedef void (*sto_req_params_deinit_t)(void *priv);
typedef void (*sto_req_priv_deinit_t)(void *priv);

typedef void (*sto_req_response_t)(struct sto_req *req, struct spdk_json_write_ctx *w);
typedef int (*sto_req_exec_constructor_t)(struct sto_req *req, int state);

struct sto_req_type {
	void *params;
	sto_req_params_deinit_t params_deinit;

	void *priv;
	sto_req_priv_deinit_t priv_deinit;

	sto_req_response_t response;
	sto_req_exec_constructor_t exec_constructor;
};

struct sto_req {
	struct sto_req_context ctx;
	struct sto_req_type type;

	int returncode;
	int state;

	TAILQ_ENTRY(sto_req) list;

	struct sto_exec_list exe_queue;
	struct sto_exec_list rollback_stack;
};

#define STO_REQ(x) \
	SPDK_CONTAINEROF((x), struct sto_req, ctx)

struct sto_req_properties {
	uint32_t params_size;
	sto_req_params_deinit_t params_deinit;

	uint32_t priv_size;
	sto_req_priv_deinit_t priv_deinit;

	sto_req_response_t response;
	sto_req_exec_constructor_t exec_constructor;
};

int sto_req_lib_init(void);
void sto_req_lib_fini(void);

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
	struct sto_req_context *ctx = &req->ctx;

	ctx->done(ctx->priv);
}

struct sto_req *sto_req_alloc(const struct sto_req_properties *properties);
int sto_req_type_parse_params(struct sto_req_type *type, const struct sto_ops_decoder *decoder,
			      const struct spdk_json_val *values,
			      sto_ops_req_params_constructor_t params_constructor);
void sto_req_free(struct sto_req *req);

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

extern const struct sto_req_properties sto_write_req_properties;

struct sto_read_req_params {
	const char *file;
	uint32_t size;
};

extern const struct sto_req_properties sto_read_req_properties;

struct sto_readlink_req_params {
	const char *file;
};

extern const struct sto_req_properties sto_readlink_req_properties;

struct sto_readdir_req_params {
	const char *name;
	char *dirpath;
#define EXCLUDE_LIST_MAX 20
	const char *exclude_list[EXCLUDE_LIST_MAX];
};

extern const struct sto_req_properties sto_readdir_req_properties;

struct sto_tree_req_params {
	char *dirpath;
	uint32_t depth;
	bool only_dirs;

	sto_tree_info_json_t info_json;
};

extern const struct sto_req_properties sto_tree_req_properties;

#endif /* _STO_REQ_H_ */
