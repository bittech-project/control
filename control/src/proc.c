/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 */
#include <spdk/event.h>
#include <spdk/log.h>
#include <spdk/string.h>

#include "subprocess.h"

static char *proc_name = "ls";
static bool capture_output;

/*
 * We'll use this struct to gather housekeeping proc_context to pass between
 * our events and callbacks.
 */
struct proc_context_t {
	char *name;
};

struct sto_subprocess_ctx subp_ctx;

/*
 * Usage function for printing parameters that are specific to this application
 */
static void
proc_usage(void)
{
	printf(" -P <proc name>                 name of the proc to execute\n");
	printf(" -O <capture output>            if set subprocess will also capture the output (default 0)\n");
}

/*
 * This function is called to parse the parameters that are specific to this application
 */
static int
proc_parse_arg(int ch, char *arg)
{
	switch (ch) {
	case 'P':
		proc_name = arg;
		break;
	case 'O':
		capture_output = spdk_strtol(arg, 10);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void proc_finish(struct sto_subprocess_ctx *subp_ctx)
{
	SPDK_NOTICELOG("Exec finish: rc=%d output=%s\n",
			subp_ctx->returncode, subp_ctx->output);
}

/*
 * Our initial event that kicks off everything from main().
 */
static void
proc_start(void *arg1)
{
	struct proc_context_t *proc_context = arg1;
	struct sto_subprocess *subp;
	const char *const argv[] = {
			proc_context->name,
	};
	int rc;

	SPDK_NOTICELOG("Successfully started the SPDK application: exec %s\n", proc_context->name);

	sto_subprocess_init();

	subp_ctx.subprocess_done = proc_finish;

	subp = sto_subprocess_create(argv, SPDK_COUNTOF(argv), capture_output, 0);
	rc = sto_subprocess_run(subp, &subp_ctx);

	/* sto_subprocess_exit(); */
}

int
main(int argc, char **argv)
{
	struct spdk_app_opts opts = {};
	int rc = 0;
	struct proc_context_t proc_context = {};

	/* Set default values in opts structure. */
	spdk_app_opts_init(&opts, sizeof(opts));
	opts.name = "proc";

	/*
	 * Parse built-in SPDK command line parameters as well
	 * as our custom one(s).
	 */
	if ((rc = spdk_app_parse_args(argc, argv, &opts, "P:O:", NULL, proc_parse_arg,
				      proc_usage)) != SPDK_APP_PARSE_ARGS_SUCCESS) {
		exit(rc);
	}
	proc_context.name = proc_name;

	/*
	 * spdk_app_start() will initialize the SPDK framework, call proc_start(),
	 * and then block until spdk_app_stop() is called (or if an initialization
	 * error occurs, spdk_app_start() will return with rc even without calling
	 * proc_start().
	 */
	rc = spdk_app_start(&opts, proc_start, &proc_context);
	if (rc) {
		SPDK_ERRLOG("ERROR starting application\n");
	}

	/*
	 * At this point either spdk_app_stop() was called, or spdk_app_start()
	 * failed because of internal error.
	 */

	/* Gracefully close out all of the SPDK subsystems. */
	spdk_app_fini();
	return rc;
}
