#ifndef _STO_AIO_FRONT_H_
#define _STO_AIO_FRONT_H_

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

struct sto_aio;
typedef void (*aio_end_io_t)(struct sto_aio *aio);

struct sto_aio {
	int dir;

	struct {
		const char *filename;
		char *buf;
		size_t size;
	};

	int returncode;

	void *priv;
	aio_end_io_t aio_end_io;
};

struct sto_aio *sto_aio_alloc(const char *filename, void *buf, size_t size, int dir);
void sto_aio_init_cb(struct sto_aio *aio, aio_end_io_t aio_end_io, void *priv);
void sto_aio_free(struct sto_aio *aio);

int sto_aio_submit(struct sto_aio *aio);

int sto_aio_write_string(const char *filename, char *str, aio_end_io_t aio_end_io, void *priv);

#endif /* _STO_AIO_FRONT_H_ */
