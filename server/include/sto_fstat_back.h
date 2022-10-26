#ifndef _STO_FSTAT_BACK_H_
#define _STO_FSTAT_BACK_H_

#include "sto_exec.h"

struct sto_fstat_back_req;
typedef void (*fstat_back_done_t)(struct sto_fstat_back_req *req);

struct sto_fstat_back_req {
	struct sto_exec_ctx exec_ctx;

	struct {
		const char *filename;
	};

	struct {
		int returncode;
		struct stat stat;
	};

	void *priv;
	fstat_back_done_t fstat_back_done;
};

int sto_fstat_back(const char *filename, fstat_back_done_t fstat_back_done, void *priv);
void sto_fstat_back_free(struct sto_fstat_back_req *req);

#endif /* _STO_FSTAT_BACK_H_ */
