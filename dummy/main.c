/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 */

#include <spdk/event.h>
#include <spdk/log.h>
#include <spdk/string.h>

static char *dummy_name = "dummy";

/*
 * We'll use this struct to gather housekeeping dummy_context to pass between
 * our events and callbacks.
 */
struct dummy_context_t {
	char *name;
};

/*
 * Usage function for printing parameters that are specific to this application
 */
static void
dummy_usage(void)
{
	printf(" -N <name>                 name of the dummy application\n");
}

/*
 * This function is called to parse the parameters that are specific to this application
 */
static int
dummy_parse_arg(int ch, char *arg)
{
	switch (ch) {
	case 'N':
		dummy_name = arg;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Our initial event that kicks off everything from main().
 */
static void
dummy_start(void *arg1)
{
	struct dummy_context_t *dummy_context = arg1;

	SPDK_NOTICELOG("Successfully started %s the application\n", dummy_context->name);
}

int
main(int argc, char **argv)
{
	struct spdk_app_opts opts = {};
	int rc = 0;
	struct dummy_context_t dummy_context = {};

	/* Set default values in opts structure. */
	spdk_app_opts_init(&opts, sizeof(opts));
	opts.name = "dummy";

	/*
	 * Parse built-in SPDK command line parameters as well
	 * as our custom one(s).
	 */
	if ((rc = spdk_app_parse_args(argc, argv, &opts, "N:", NULL, dummy_parse_arg,
				      dummy_usage)) != SPDK_APP_PARSE_ARGS_SUCCESS) {
		exit(rc);
	}
	dummy_context.name = dummy_name;

	/*
	 * spdk_app_start() will initialize the SPDK framework, call dummy_start(),
	 * and then block until spdk_app_stop() is called (or if an initialization
	 * error occurs, spdk_app_start() will return with rc even without calling
	 * dummy_start().
	 */
	rc = spdk_app_start(&opts, dummy_start, &dummy_context);
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
