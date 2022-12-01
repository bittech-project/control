#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/string.h>

#include <rte_malloc.h>

#include "sto_client.h"
#include "sto_rpc_subprocess.h"

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
sto_subprocess_resp_handler(void *priv, struct spdk_jsonrpc_client_response *resp, int rc)
{
	struct sto_subprocess *subp = priv;
	struct sto_subprocess_result result;

	if (spdk_unlikely(rc)) {
		subp->returncode = rc;
		goto out;
	}

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

free_params:
	sto_subprocess_result_free(&result);

out:
	subp->subprocess_done(subp);
}

static void
sto_subprocess_info_json(void *priv, struct spdk_json_write_ctx *w)
{
	struct sto_subprocess *subp = priv;
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
	struct sto_client_args args = {
		.priv = subp,
		.response_handler = sto_subprocess_resp_handler,
	};

	return sto_client_send("subprocess", subp, sto_subprocess_info_json, &args);
}

int
sto_subprocess_exec(const char *const cmd[], int numargs,
		    subprocess_done_t cmd_done, void *priv)
{
	struct sto_subprocess *subp;
	int rc = 0;

	subp = sto_subprocess_alloc(cmd, numargs, false);
	if (spdk_unlikely(!subp)) {
		SPDK_ERRLOG("Failed to create subprocess\n");
		return -ENOMEM;
	}

	sto_subprocess_init_cb(subp, cmd_done, priv);

	rc = sto_subprocess_run(subp);
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to run subprocess, rc=%d\n", rc);
		goto free_subp;
	}

	return 0;

free_subp:
	sto_subprocess_free(subp);

	return rc;
}
