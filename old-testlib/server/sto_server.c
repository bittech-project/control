#include "sto_server.h"

#include <sys/file.h>

#include <spdk/stdinc.h>
#include <spdk/jsonrpc.h>
#include <spdk/queue.h>
#include <spdk/likely.h>
#include <spdk/string.h>
#include <spdk/env_dpdk.h>
#include <spdk/json.h>

#include "sto_rpc.h"

struct spdk_jsonrpc_request;

#define UNIX_PATH_MAX 108

struct sto_server {
	pid_t pid;
	bool initialized;

	const char *listen_addr;
	char lock_path[UNIX_PATH_MAX + sizeof(".lock")];
	int lock_fd;

	struct {
		struct sockaddr_un listen_addr_unix;
		struct spdk_jsonrpc_server *s;
	};
};

struct sto_rpc_method {
	const char *name;
	sto_rpc_method_handler func;
	SLIST_ENTRY(sto_rpc_method) slist;
};

static struct sto_server g_sto_server;
static bool g_server_is_running;

static SLIST_HEAD(, sto_rpc_method) g_rpc_methods = SLIST_HEAD_INITIALIZER(g_rpc_methods);
static bool g_rpcs_correct = true;


static bool
sto_rpc_verify_methods(void)
{
	return g_rpcs_correct;
}

static struct sto_rpc_method *
_get_rpc_method(const struct spdk_json_val *method)
{
	struct sto_rpc_method *m;

	SLIST_FOREACH(m, &g_rpc_methods, slist) {
		if (spdk_json_strequal(method, m->name)) {
			return m;
		}
	}

	return NULL;
}

static struct sto_rpc_method *
_get_rpc_method_raw(const char *method)
{
	struct spdk_json_val method_val;

	method_val.type = SPDK_JSON_VAL_STRING;
	method_val.len = strlen(method);
	method_val.start = (char *)method;

	return _get_rpc_method(&method_val);
}

void
sto_rpc_register_method(const char *method, sto_rpc_method_handler func)
{
	struct sto_rpc_method *m;

	m = _get_rpc_method_raw(method);
	if (m != NULL) {
		printf("duplicate RPC %s registered...\n", method);
		g_rpcs_correct = false;
		return;
	}

	m = calloc(1, sizeof(*m));
	assert(m != NULL);

	m->name = strdup(method);
	assert(m->name != NULL);

	m->func = func;

	/* TODO: use a hash table or sorted list */
	SLIST_INSERT_HEAD(&g_rpc_methods, m, slist);
}

static void
sto_jsonrpc_handler(struct spdk_jsonrpc_request *request,
		    const struct spdk_json_val *method,
		    const struct spdk_json_val *params)
{
	struct sto_rpc_method *m;

	assert(method != NULL);

	m = _get_rpc_method(method);
	if (m == NULL) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_METHOD_NOT_FOUND, "Method not found");
		return;
	}

	m->func(request, params);
}

static int
sto_server_listen(struct sto_server *s)
{
	struct sockaddr_un *addr;
	int rc;

	addr = &s->listen_addr_unix;

	memset(addr, 0, sizeof(*addr));

	addr->sun_family = AF_UNIX;
	rc = snprintf(addr->sun_path, sizeof(addr->sun_path), "%s", s->listen_addr);
	if (rc < 0 || (size_t)rc >= sizeof(addr->sun_path)) {
		printf("STO Listen address Unix socket path too long\n");
		addr->sun_path[0] = '\0';
		return -EINVAL;
	}

	rc = snprintf(s->lock_path, sizeof(s->lock_path), "%s.lock", addr->sun_path);
	if (rc < 0 || (size_t)rc >= sizeof(s->lock_path)) {
		printf("STO lock path too long\n");
		addr->sun_path[0] = '\0';
		s->lock_path[0] = '\0';
		return -EINVAL;
	}

	s->lock_fd = open(s->lock_path, O_RDONLY | O_CREAT, 0600);
	if (s->lock_fd == -1) {
		printf("Cannot open lock file %s: %s\n",
		       s->lock_path, spdk_strerror(errno));
		addr->sun_path[0] = '\0';
		s->lock_path[0] = '\0';
		return -errno;
	}

	rc = flock(s->lock_fd, LOCK_EX | LOCK_NB);
	if (rc != 0) {
		printf("RPC Unix domain socket path %s in use. Specify another.\n",
		       addr->sun_path);
		addr->sun_path[0] = '\0';
		s->lock_path[0] = '\0';
		return rc;
	}

	/*
	 * Since we acquired the lock, it is safe to delete the Unix socket file
	 * if it still exists from a previous process.
	 */
	unlink(addr->sun_path);

	s->s = spdk_jsonrpc_server_listen(AF_UNIX, 0,
					  (struct sockaddr *) addr, sizeof(*addr), sto_jsonrpc_handler);
	if (s->s == NULL) {
		printf("spdk_jsonrpc_server_listen() failed\n");
		close(s->lock_fd);
		s->lock_fd = -1;
		unlink(addr->sun_path);
		addr->sun_path[0] = '\0';
		return -1;
	}

	return 0;
}

static void
spdk_server_close(struct sto_server *s)
{
	struct sockaddr_un *addr;

	if (spdk_unlikely(!s)) {
		return;
	}

	addr = &s->listen_addr_unix;

	if (addr->sun_path[0]) {
		/* Delete the Unix socket file */
		unlink(addr->sun_path);
		addr->sun_path[0] = '\0';
	}

	spdk_jsonrpc_server_shutdown(s->s);
	s->s = NULL;

	if (s->lock_fd != -1) {
		close(s->lock_fd);
		s->lock_fd = -1;
	}

	if (s->lock_path[0]) {
		unlink(s->lock_path);
		s->lock_path[0] = '\0';
	}

	printf("STO server close\n");
}

static int
sto_server_accept_loop(struct sto_server *s)
{
	int rc = 0;

	while (g_server_is_running) {
		rc = spdk_jsonrpc_server_poll(s->s);
	}

	return rc;
}

static void
sig_int(int sig)
{
	/* Just ignore for now */
}

static void
sig_term(int sig)
{
	g_server_is_running = false;
}

static int
set_sig_handlers(void)
{
	struct sigaction act;
	int rc;

	memset(&act, 0, sizeof(act));
	act.sa_handler = sig_int;
	act.sa_flags = SA_RESTART;

	rc = sigaction(SIGINT, &act, NULL);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to set SIGINT handler\n");
		return -errno;
	}

	memset(&act, 0, sizeof(act));
	act.sa_handler = sig_term;
	act.sa_flags = SA_RESTART;

	rc = sigaction(SIGTERM, &act, NULL);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to set SIGTERM handler\n");
		return -errno;
	}

	return 0;
}

static int
sto_server_run(struct sto_server *s)
{
	int rc = 0;

	printf("STO server run\n");

	g_server_is_running = true;

	rc = set_sig_handlers();
	if (spdk_unlikely(rc)) {
		printf("Failed to set signal handlers: %d\n", rc);
		return rc;
	}

	rc = sto_server_listen(s);
	if (spdk_unlikely(rc)) {
		printf("Failed to start listen: %d\n", rc);
		return rc;
	}

	rc = sto_server_accept_loop(s);

	spdk_server_close(s);

	return rc;
}

static void
sto_server_init(struct sto_server *s)
{
	memset(s, 0, sizeof(*s));

	s->listen_addr = STO_LOCAL_SERVER_ADDR;
	s->lock_path[0] = '\0';
	s->lock_fd = -1;
}

int
sto_server_start(void)
{
	pid_t pid;
	int rc = 0;

	printf("STO server start\n");

	if (g_sto_server.initialized) {
		printf("FAILED: STO exec server has already been initialized\n");
		return -EINVAL;
	}

	if (!sto_rpc_verify_methods()) {
		printf("FAILED: Some RPC methods has not been registered\n");
		return -EINVAL;
	}

	if (!spdk_env_dpdk_external_init()) {
		printf("FAILED: SPDK has already been initialized\n");
		return -EINVAL;
	}

	sto_server_init(&g_sto_server);

	pid = fork();
	if (pid == -1) {
		printf("Failed to fork: %s\n", spdk_strerror(errno));
		return -errno;
	}

	/* Child */
	if (!pid) {
		rc = sto_server_run(&g_sto_server);

		exit(rc);
	}

	g_sto_server.pid = pid;
	g_sto_server.initialized = true;

	printf("STO server started\n");

	return 0;
}

void
sto_server_fini(void)
{
	int rc, status;

	printf("STO server start fini\n");

	if (!g_sto_server.initialized) {
		printf("FAILED: STO exec server has not been initialized yet\n");
		return;
	}

	rc = kill(g_sto_server.pid, SIGTERM);
	if (rc == -1) {
		printf("FAILED: to send SIGTERM to the server: %s\n",
		       strerror(errno));
		return;
	}

	rc = waitpid(g_sto_server.pid, &status, 0);

	g_sto_server.initialized = false;

	printf("STO server end fini\n");
}
