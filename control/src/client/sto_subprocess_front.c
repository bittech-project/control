#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_client.h"
#include "sto_subprocess_front.h"

struct sto_subprocess *
sto_subprocess_alloc(const char *const argv[], int numargs, bool capture_output)
{
	struct sto_subprocess *subp;
	unsigned int data_len;
	int i;

	if (spdk_unlikely(!numargs)) {
		SPDK_ERRLOG("Too few arguments\n");
		return NULL;
	}

	/* Count the number of bytes for the 'numargs' arguments to be allocated */
	data_len = numargs * sizeof(char *);

	subp = rte_zmalloc(NULL, sizeof(*subp) + data_len, 0);
	if (spdk_unlikely(!subp)) {
		SPDK_ERRLOG("Cann't allocate memory for subprocess\n");
		return NULL;
	}

	subp->numargs = numargs;
	subp->capture_output = capture_output;

	for (i = 0; i < subp->numargs; i++) {
		subp->args[i] = argv[i];
	}

	return subp;
}

void
sto_subprocess_init_cb(struct sto_subprocess *subp,
		       subprocess_done_t subprocess_done, void *priv)
{
	subp->subprocess_done = subprocess_done;
	subp->priv = priv;
}

void
sto_subprocess_free(struct sto_subprocess *subp)
{
	free(subp->output);
	rte_free(subp);
}

struct sto_subprocess_result {
	int returncode;
	char *output;
};

static void
sto_subprocess_result_free(struct sto_subprocess_result *result)
{
	free(result->output);
}

static const struct spdk_json_object_decoder sto_subprocess_result_decoders[] = {
	{"returncode", offsetof(struct sto_subprocess_result, returncode), spdk_json_decode_int32},
	{"output", offsetof(struct sto_subprocess_result, output), spdk_json_decode_string},
};

static void
sto_subprocess_resp_handler(struct sto_rpc_request *rpc_req,
			    struct spdk_jsonrpc_client_response *resp)
{
	struct sto_subprocess *subp = rpc_req->priv;
	struct sto_subprocess_result result;

	memset(&result, 0, sizeof(result));

	if (spdk_json_decode_object(resp->result, sto_subprocess_result_decoders,
				    SPDK_COUNTOF(sto_subprocess_result_decoders), &result)) {
		SPDK_ERRLOG("Failed to decode response for subprocess\n");
		goto out;
	}

	subp->returncode = result.returncode;

	if (subp->capture_output) {
		subp->output = strdup(result.output);
		if (spdk_unlikely(!subp->output)) {
			SPDK_ERRLOG("Failed copy output for subprocess\n");
			subp->returncode = -ENOMEM;
			goto free_params;
		}
	}

	SPDK_NOTICELOG("GLEB: Get result from subprocess response: returncode[%d] output: %s\n",
		       subp->returncode, subp->output);

free_params:
	sto_subprocess_result_free(&result);

out:
	sto_rpc_req_free(rpc_req);

	subp->subprocess_done(subp);
}

static void
sto_subprocess_info_json(struct sto_rpc_request *rpc_req, struct spdk_json_write_ctx *w)
{
	struct sto_subprocess *subp = rpc_req->priv;
	int i;

	spdk_json_write_object_begin(w);

	spdk_json_write_named_array_begin(w, "cmd");
	for (i = 0; i < subp->numargs; i++) {
		spdk_json_write_string(w, subp->args[i]);
	}
	spdk_json_write_array_end(w);

	spdk_json_write_named_bool(w, "capture_output", subp->capture_output);

	spdk_json_write_object_end(w);
}

int
sto_subprocess_run(struct sto_subprocess *subp)
{
	struct sto_rpc_request *rpc_req;
	int rc = 0;

	rpc_req = sto_rpc_req_alloc("subprocess", sto_subprocess_info_json, subp);
	if (spdk_unlikely(!rpc_req)) {
		SPDK_ERRLOG("Failed to alloc RPC req\n");
		return -ENOMEM;
	}

	sto_rpc_req_init_cb(rpc_req, sto_subprocess_resp_handler);

	rc = sto_client_send(rpc_req);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to send RPC req, rc=%d\n", rc);
		sto_rpc_req_free(rpc_req);
	}

	return rc;
}
