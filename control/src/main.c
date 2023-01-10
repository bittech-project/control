#include <spdk/event.h>
#include <spdk/log.h>
#include <spdk/likely.h>

#include "sto_version.h"
#include "sto_client.h"
#include "sto_core.h"
#include <sto_server.h>

/*
 * Usage function for printing parameters that are specific to this application
 */
static void
sto_control_usage(void)
{
}

/*
 * This function is called to parse the parameters that are specific to this application
 */
static int
sto_control_parse_arg(int ch, char *arg)
{
	return 0;
}

/*
 * Our initial event that kicks off everything from main().
 */
static void
sto_control_started(void *arg1)
{
	int rc = 0;

	rc = sto_client_connect(STO_LOCAL_SERVER_ADDR, AF_UNIX);
	if (rc < 0) {
		SPDK_ERRLOG("sto_client_connect() failed, rc=%d\n", rc);
		goto out_err;
	}

	rc = sto_core_init();
	if (rc < 0) {
		SPDK_ERRLOG("sto_core_init() failed, rc=%d\n", rc);
		goto client_close;
	}

	SPDK_NOTICELOG("Successfully started the %s SPDK application\n",
		       STO_VERSION_STRING);

	return;

client_close:
	sto_client_close();

out_err:
	spdk_app_stop(rc);
}

static void
sto_control_shutdown(void)
{
	sto_core_fini();
	sto_client_close();

	spdk_app_stop(0);
}

int
main(int argc, char **argv)
{
	struct spdk_app_opts opts = {};
	int rc = 0;

	rc = sto_server_start();
	if (spdk_unlikely(rc)) {
		SPDK_ERRLOG("Failed to start server, rc=%d\n", rc);
		return rc;
	}

	/* Set default values in opts structure. */
	spdk_app_opts_init(&opts, sizeof(opts));
	opts.name = "control";

	/*
	 * Parse built-in SPDK command line parameters as well
	 * as our custom one(s).
	 */
	if ((rc = spdk_app_parse_args(argc, argv, &opts, "", NULL, sto_control_parse_arg,
				      sto_control_usage)) != SPDK_APP_PARSE_ARGS_SUCCESS) {
		exit(rc);
	}

	opts.shutdown_cb = sto_control_shutdown;

	/*
	 * spdk_app_start() will initialize the SPDK framework, call sto_control_started(),
	 * and then block until spdk_app_stop() is called (or if an initialization
	 * error occurs, spdk_app_start() will return with rc even without calling
	 * sto_control_started().
	 */
	rc = spdk_app_start(&opts, sto_control_started, NULL);
	if (rc) {
		SPDK_ERRLOG("ERROR starting application\n");
	}

	/*
	 * At this point either spdk_app_stop() was called, or spdk_app_start()
	 * failed because of internal error.
	 */

	/* Gracefully close out all of the SPDK subsystems. */
	spdk_app_fini();

	sto_server_fini();

	return rc;
}
