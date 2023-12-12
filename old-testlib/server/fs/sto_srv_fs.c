#include <spdk/stdinc.h>
#include <spdk/json.h>
#include <spdk/likely.h>
#include <spdk/string.h>
#include <spdk/queue.h>

#include "sto_srv_fs.h"

struct spdk_json_write_ctx;

struct sto_srv_dirent *
sto_srv_dirent_alloc(const char *name)
{
	struct sto_srv_dirent *dirent;

	dirent = calloc(1, sizeof(*dirent));
	if (spdk_unlikely(!dirent)) {
		printf("server: Failed to alloc dirent\n");
		return NULL;
	}

	dirent->name = strdup(name);
	if (spdk_unlikely(!dirent->name)) {
		printf("server: Failed to alloc dirent name\n");
		goto free_dirent;
	}

	return dirent;

free_dirent:
	free(dirent);

	return NULL;
}

void
sto_srv_dirent_free(struct sto_srv_dirent *dirent)
{
	free(dirent->name);
	free(dirent);
}

int
sto_srv_dirent_get_stat(struct sto_srv_dirent *dirent, const char *path)
{
	struct stat sb;
	char *full_path;
	int rc = 0;

	full_path = spdk_sprintf_alloc("%s/%s", path, dirent->name);
	if (spdk_unlikely(!full_path)) {
		return -ENOMEM;
	}

	if (lstat(full_path, &sb) == -1) {
		printf("server: Failed to get stat for file %s: %s\n",
		       full_path, strerror(errno));
		rc = -errno;
		goto out;
	}

	dirent->mode = sb.st_mode;

out:
	free(full_path);

	return rc;
}

static void
sto_srv_dirent_info_json(struct sto_srv_dirent *dirent, struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_named_string(w, "name", dirent->name);
	spdk_json_write_named_uint32(w, "mode", dirent->mode);

	spdk_json_write_object_end(w);
}

void
sto_srv_dirents_init(struct sto_srv_dirents *dirents)
{
	TAILQ_INIT(&dirents->dirents);
}

void
sto_srv_dirents_free(struct sto_srv_dirents *dirents)
{
	struct sto_srv_dirent *dirent, *tmp;

	TAILQ_FOREACH_SAFE(dirent, &dirents->dirents, list, tmp) {
		TAILQ_REMOVE(&dirents->dirents, dirent, list);

		sto_srv_dirent_free(dirent);
	}
}

void
sto_srv_dirents_add(struct sto_srv_dirents *dirents, struct sto_srv_dirent *dirent)
{
	TAILQ_INSERT_TAIL(&dirents->dirents, dirent, list);
}

void
sto_srv_dirents_info_json(struct sto_srv_dirents *dirents,
			  struct spdk_json_write_ctx *w)
{
	struct sto_srv_dirent *dirent;

	spdk_json_write_named_array_begin(w, "dirents");

	TAILQ_FOREACH(dirent, &dirents->dirents, list) {
		sto_srv_dirent_info_json(dirent, w);
	}

	spdk_json_write_array_end(w);
}

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
sto_read_file(const char *filepath, void *data, size_t size)
{
	int fd, rc;

	fd = open(filepath, O_RDONLY);
	if (spdk_unlikely(fd == -1)) {
		printf("Failed to open %s file\n", filepath);
		return -errno;
	}

	rc = sto_read(fd, data, size);
	if (spdk_unlikely(rc)) {
		printf("Failed to read %s file\n", filepath);
		return rc;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to close %s file\n", filepath);
	}

	return 0;
}

int
sto_write_file(const char *filepath, int oflag, void *data, size_t size)
{
	int fd, rc;

	fd = open(filepath, O_WRONLY | oflag);
	if (spdk_unlikely(fd == -1)) {
		printf("Failed to open %s file\n", filepath);
		return -errno;
	}

	rc = sto_write(fd, data, size);
	if (spdk_unlikely(rc)) {
		printf("Failed to write %s file\n", filepath);
		return rc;
	}

	rc = close(fd);
	if (spdk_unlikely(rc == -1)) {
		printf("Failed to close %s file\n", filepath);
	}

	return 0;
}
