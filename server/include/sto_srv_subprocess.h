#ifndef _STO_SRV_SUBPROCESS_H_
#define _STO_SRV_SUBPROCESS_H_

typedef void (*sto_srv_subprocess_done_t)(void *priv, char *output, int rc);

struct sto_srv_subprocess_args {
	void *priv;
	sto_srv_subprocess_done_t done;
};

int sto_srv_subprocess(const char *const argv[], int numargs, bool capture_output,
		       struct sto_srv_subprocess_args *args);

#endif /* _STO_SRV_SUBPROCESS_H_ */
