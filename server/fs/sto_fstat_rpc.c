#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include "sto_rpc.h"
#include "sto_fstat_back.h"

struct sto_fstat_params {
	char *filename;
};

static void
sto_fstat_params_free(struct sto_fstat_params *params)
{
	free(params->filename);
}

static const struct spdk_json_object_decoder sto_fstat_decoders[] = {
	{"filename", offsetof(struct sto_fstat_params, filename), spdk_json_decode_string},
};

static void
sto_fstat_response(struct sto_fstat_back_req *req, struct spdk_jsonrpc_request *request)
{
	struct spdk_json_write_ctx *w;
	struct stat *stat = &req->stat;

	w = spdk_jsonrpc_begin_result(request);

	spdk_json_write_object_begin(w);

	spdk_json_write_named_int32(w, "returncode", req->returncode);

	/* dev_t     st_dev; ID of device containing file */
	spdk_json_write_named_uint64(w, "st_dev", stat->st_dev);

	/* ino_t     st_ino; inode number */
	spdk_json_write_named_uint64(w, "st_ino", stat->st_ino);

	/* mode_t    st_mode; protection */
	spdk_json_write_named_uint32(w, "st_mode", stat->st_mode);

	/* nlink_t   st_nlink; number of hard links */
	spdk_json_write_named_uint64(w, "st_nlink", stat->st_nlink);

	/* uid_t     st_uid; user ID of owner */
	spdk_json_write_named_uint64(w, "st_uid", stat->st_uid);

	/* gid_t     st_gid; group ID of owner */
	spdk_json_write_named_uint64(w, "st_gid", stat->st_gid);

	/* dev_t     st_rdev; device ID (if special file) */
	spdk_json_write_named_uint64(w, "st_rdev", stat->st_rdev);

	/* off_t     st_size; total size, in bytes */
	spdk_json_write_named_uint64(w, "st_size", stat->st_size);

	/* blksize_t st_blksize; blocksize for file system I/O */
	spdk_json_write_named_uint64(w, "st_blksize", stat->st_blksize);

	/* blkcnt_t  st_blocks; number of 512B blocks allocated */
	spdk_json_write_named_uint64(w, "st_blocks", stat->st_blocks);

	/* time_t    st_atime; time of last access */
	spdk_json_write_named_uint64(w, "st_atime", stat->st_atime);

	/* time_t    st_mtime; time of last modification */
	spdk_json_write_named_uint64(w, "st_mtime", stat->st_mtime);

	/* time_t    st_ctime; time of last status change */
	spdk_json_write_named_uint64(w, "st_ctime", stat->st_ctime);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_result(request, w);
}

static void
sto_fstat_done(struct sto_fstat_back_req *req)
{
	struct spdk_jsonrpc_request *request = req->priv;

	sto_fstat_response(req, request);

	sto_fstat_back_free(req);
}

static void
sto_rpc_fstat(struct spdk_jsonrpc_request *request, const struct spdk_json_val *params)
{
	struct sto_fstat_params rd_params = {};
	int rc;

	if (spdk_json_decode_object(params, sto_fstat_decoders,
				    SPDK_COUNTOF(sto_fstat_decoders), &rd_params)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		return;
	}

	rc = sto_fstat_back(rd_params.filename, sto_fstat_done, request);
	if (spdk_unlikely(rc)) {
		spdk_jsonrpc_send_error_response(request, rc, strerror(-rc));
		goto out;
	}

out:
	sto_fstat_params_free(&rd_params);

	return;
}
STO_RPC_REGISTER("fstat", sto_rpc_fstat)
