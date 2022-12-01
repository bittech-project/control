#ifndef _STO_SRV_SUBPROCESS_H_
#define _STO_SRV_SUBPROCESS_H_

#include <spdk/util.h>

#include "sto_exec.h"

struct sto_srv_subprocess_req;
typedef void (*sto_srv_subprocess_done_t)(struct sto_srv_subprocess_req *req);

struct sto_srv_subprocess_req {
	struct sto_exec_ctx exec_ctx;

	bool capture_output;

	int returncode;
	char output[256];
	size_t output_sz;

	int pipefd[2];

	void *priv;
	sto_srv_subprocess_done_t done;

	int numargs;
	const char *file;
	const char *args[];
};

#define STO_SUBPROCESS_BACK(ARGV)	\
	(sto_srv_subprocess_req_alloc((ARGV), SPDK_COUNTOF((ARGV)), false))

struct sto_srv_subprocess_req *sto_srv_subprocess_req_alloc(const char *const argv[],
		int numargs, bool capture_output);
void sto_srv_subprocess_req_free(struct sto_srv_subprocess_req *req);
void sto_srv_subprocess_req_init_cb(struct sto_srv_subprocess_req *req,
				    sto_srv_subprocess_done_t done, void *priv);

int sto_srv_subprocess_req_submit(struct sto_srv_subprocess_req *req);

#endif /* _STO_SRV_SUBPROCESS_H_ */
