#include <spdk/likely.h>

#include "sto_aio.h"

static int sto_aio_exec(void *arg);
static void sto_aio_exec_done(void *arg, int rc);

static struct sto_exec_ops aio_ops = {
	.name = "aio",
	.exec = sto_aio_exec,
	.exec_done = sto_aio_exec_done,
};

static void
sto_stdout_choker(int action)
{
	static int STO_STDOUT_FD = 0;
	static FILE *STO_STDOUT_FNULL = NULL;

	switch (action) {
	case 1:
		STO_STDOUT_FD = dup(STDOUT_FILENO);

		fflush(stdout);
		STO_STDOUT_FNULL = fopen("/dev/null", "w");
		dup2(fileno(STO_STDOUT_FNULL), STDOUT_FILENO);
		break;
	case 0:
		dup2(STO_STDOUT_FD, STDOUT_FILENO);
		close(STO_STDOUT_FD);
		break;
	default:
		printf("Invalid action - %d\n", action);
		break;
	}
}

static void
sto_stderr_choker(int action)
{
	static int STO_STDERR_FD = 0;
	static FILE *STO_STDERR_FNULL = NULL;

	switch (action) {
	case 1:
		STO_STDERR_FD = dup(STDERR_FILENO);

		fflush(stderr);
		STO_STDERR_FNULL = fopen("/dev/null", "w");
		dup2(fileno(STO_STDERR_FNULL), STDERR_FILENO);
		break;
	case 0:
		dup2(STO_STDERR_FD, STDERR_FILENO);
		close(STO_STDERR_FD);
		break;
	default:
		printf("Invalid action - %d\n", action);
		break;
	}
}

void
sto_choker_on(void)
{
	sto_stdout_choker(1);
	sto_stderr_choker(1);
}

void
sto_choker_off(void)
{
	sto_stdout_choker(0);
	sto_stderr_choker(0);
}

int
sto_read(int fd, void *data, size_t size)
{
	ssize_t ret;
	int rc = 0;

	while (size) {
		ret = read(fd, data, size);
		if (ret == -1) {
			if (errno == EINTR) {
				continue;
			}

			printf("Failed to read from %d fd: %s\n",
			       fd, strerror(errno));
			rc = -errno;
			break;
		}

		if (!ret) {
			break;
		}

		data += ret;
		size -= ret;
	}

	return rc;
}

int
sto_write(int fd, void *data, size_t size)
{
	ssize_t ret;
	int rc = 0;

	while (size) {
		ret = write(fd, data, size);
		if (ret == -1) {
			if (errno == EINTR) {
				continue;
			}

			printf("Failed to write to %d fd: %s\n",
			       fd, strerror(errno));
			rc = -errno;
			break;
		}

		if (!ret) {
			break;
		}

		data += ret;
		size -= ret;
	}

	return rc;
}

int
sto_read_data(const char *filename, void *data, size_t size)
{
	int fd, rc;

	fd = open(filename, O_RDONLY);
	if (spdk_unlikely(fd == -1)) {
		printf("Failed to open %s file\n", filename);
		return -errno;
	}

	rc = sto_read(fd, data, size);
	if (spdk_unlikely(rc)) {
		printf("Failed to read %s file\n", filename);
		return rc;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to close %s file\n", filename);
	}

	return 0;
}

int
sto_write_data(const char *filename, void *data, size_t size)
{
	int fd, rc;

	fd = open(filename, O_WRONLY);
	if (spdk_unlikely(fd == -1)) {
		printf("Failed to open %s file\n", filename);
		return -errno;
	}

	rc = sto_write(fd, data, size);
	if (spdk_unlikely(rc)) {
		printf("Failed to write %s file\n", filename);
		return rc;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to close %s file\n", filename);
	}

	return 0;
}

static int
sto_aio_exec(void *arg)
{
	struct sto_aio *aio = arg;
	int rc = 0;

	if (aio->dir == STO_WRITE) {
		rc = sto_write_data(aio->filename, aio->buf, aio->size);
	} else {
		rc = sto_read_data(aio->filename, aio->buf, aio->size);
	}

	if (spdk_unlikely(rc)) {
		printf("Failed to write/read %zu to/from %s file\n",
		       aio->size, aio->filename);
	}

	return rc;
}

static void
sto_aio_exec_done(void *arg, int rc)
{
	struct sto_aio *aio = arg;

	aio->rc = rc;
	aio->aio_end_io(aio);
}

struct sto_aio *
sto_aio_alloc(const char *filename, void *buf, size_t size, int dir)
{
	struct sto_aio *aio;

	aio = calloc(1, sizeof(*aio));
	if (spdk_unlikely(!aio)) {
		printf("Cann't allocate memory for STO aio\n");
		return NULL;
	}

	aio->filename = strdup(filename);
	if (spdk_unlikely(!aio->filename)) {
		printf("Cann't allocate memory for filename: %s\n", filename);
		goto free_aio;
	}

	sto_exec_init_ctx(&aio->exec_ctx, &aio_ops, aio);

	aio->buf = buf;
	aio->size = size;
	aio->dir = dir;

	return aio;

free_aio:
	free(aio);

	return NULL;
}

void
sto_aio_init_cb(struct sto_aio *aio, aio_end_io_t aio_end_io, void *priv)
{
	aio->aio_end_io = aio_end_io;
	aio->priv = priv;
}

void
sto_aio_free(struct sto_aio *aio)
{
	free((char *) aio->filename);
	free(aio);
}

int
sto_aio_submit(struct sto_aio *aio)
{
	int rc;

	rc = sto_exec(&aio->exec_ctx);

	return rc;
}
