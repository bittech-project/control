#include <spdk/stdinc.h>
#include <spdk/event.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <sto_server.h>

#include <sto_server.h>

#include "sto_control.h"
#include "sto_version.h"
#include "sto_client.h"
#include "sto_core.h"
#include "sto_err.h"

static bool g_control_initialized;

bool
sto_control_is_initialized(void)
{
	return g_control_initialized;
}

/*
 * Usage function for printing parameters that are specific to this application
 */
static void
control_usage(void)
{
}

/*
 * This function is called to parse the parameters that are specific to this application
 */
static int
control_parse_arg(int ch, char *arg)
{
	return 0;
}

static void control_shutdown(int rc);

static void
control_core_init_done(void *cb_arg, int rc)
{
	if (spdk_unlikely(rc)) {
		goto out_err;
	}

	SPDK_NOTICELOG("Successfully started the %s SPDK application\n",
		       STO_VERSION_STRING);

	g_control_initialized = true;

	return;

out_err:
	control_shutdown(rc);
}

/*
 * Our initial event that kicks off everything from main().
 */
static void
control_start(void *arg1)
{
	int rc = 0;

	rc = sto_client_connect(STO_LOCAL_SERVER_ADDR, AF_UNIX);
	if (rc < 0) {
		SPDK_ERRLOG("sto_client_connect() failed, rc=%d\n", rc);
		spdk_app_stop(rc);
		return;
	}

	sto_core_init(control_core_init_done, NULL);
}

static void
control_core_fini_done(void *cb_arg)
{
	int rc = PTR_ERR(cb_arg);

	sto_client_close();

	spdk_app_stop(rc);
}

static void
control_shutdown(int rc)
{
	sto_core_fini(control_core_fini_done, ERR_PTR(rc));
}

static void
control_shutdown_cb(void)
{
	control_shutdown(0);
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
	if ((rc = spdk_app_parse_args(argc, argv, &opts, "", NULL, control_parse_arg,
				      control_usage)) != SPDK_APP_PARSE_ARGS_SUCCESS) {
		exit(rc);
	}

	opts.shutdown_cb = control_shutdown_cb;

	/*
	 * spdk_app_start() will initialize the SPDK framework, call control_start(),
	 * and then block until spdk_app_stop() is called (or if an initialization
	 * error occurs, spdk_app_start() will return with rc even without calling
	 * control_start().
	 */
	rc = spdk_app_start(&opts, control_start, NULL);
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
