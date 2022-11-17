#ifndef _STO_SRV_AIO_H_
#define _STO_SRV_AIO_H_

#include "sto_exec.h"

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

void sto_choker_on(void);
void sto_choker_off(void);

int sto_write(int fd, void *data, size_t size);
int sto_read(int fd, void *data, size_t size);

int sto_write_file(const char *filepath, void *data, size_t size);
int sto_read_file(const char *filepath, void *data, size_t size);

struct spdk_json_val;
typedef void (*sto_srv_writefile_done_t)(void *priv, int rc);

struct sto_srv_writefile_args {
	void *priv;
	sto_srv_writefile_done_t done;
};

int sto_srv_writefile(const struct spdk_json_val *params,
		      struct sto_srv_writefile_args *args);

#endif /* _STO_SRV_AIO_H_ */
