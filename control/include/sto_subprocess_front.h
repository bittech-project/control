#ifndef _STO_SUBPROCESS_FRONT_H_
#define _STO_SUBPROCESS_FRONT_H_

#include <spdk/util.h>

struct sto_subprocess;
typedef void (*subprocess_done_t)(struct sto_subprocess *subp);

struct sto_subprocess {
	struct {
		int returncode;
		char *output;
	}; /* result related fields */

	bool capture_output;

	void *priv;
	subprocess_done_t subprocess_done;

	int numargs;
	const char *args[];
};

#define STO_SUBPROCESS(ARGV)	\
	(sto_subprocess_alloc((ARGV), SPDK_COUNTOF((ARGV)), false))

struct sto_subprocess *sto_subprocess_alloc(const char *const argv[],
		int numargs, bool capture_output);
void sto_subprocess_free(struct sto_subprocess *subp);
void sto_subprocess_init_cb(struct sto_subprocess *subp,
			    subprocess_done_t subprocess_done, void *priv);

int sto_subprocess_run(struct sto_subprocess *subp);

#endif /* _STO_SUBPROCESS_FRONT_H_ */
