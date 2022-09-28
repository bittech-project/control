#ifndef _STO_AIO_H_
#define _STO_AIO_H_

#include "sto_exec.h"

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

struct sto_aio;
typedef void (*aio_end_io_t)(struct sto_aio *aio);

struct sto_aio {
	struct sto_exec_ctx exec_ctx;

	struct {
		const char *filename;
		char *buf;
		size_t size;
	};

	int dir;
	int rc;

	void *priv;
	aio_end_io_t aio_end_io;
};

void sto_choker_on(void);
void sto_choker_off(void);

int sto_write(int fd, void *data, size_t size);
int sto_read(int fd, void *data, size_t size);

int sto_write_data(const char *filename, void *data, size_t size);
int sto_read_data(const char *filename, void *data, size_t size);

struct sto_aio *sto_aio_alloc(const char *filename, void *buf, size_t size, int dir);
void sto_aio_free(struct sto_aio *aio);
void sto_aio_init_cb(struct sto_aio *aio, aio_end_io_t aio_end_io, void *priv);

int sto_aio_submit(struct sto_aio *aio);


#endif /* _STO_AIO_H_ */
