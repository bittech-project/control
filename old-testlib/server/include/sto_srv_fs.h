#ifndef _STO_SRV_FS_H_
#define _STO_SRV_FS_H_

#include <spdk/queue.h>

/* generic data direction definitions */
#define STO_READ	0
#define STO_WRITE	1

struct spdk_json_write_ctx;

struct sto_srv_dirent {
	char *name;
	uint32_t mode;

	TAILQ_ENTRY(sto_srv_dirent) list;
};

struct sto_srv_dirents {
	TAILQ_HEAD(, sto_srv_dirent) dirents;
};

void sto_choker_on(void);
void sto_choker_off(void);

int sto_write(int fd, void *data, size_t size);
int sto_read(int fd, void *data, size_t size);

int sto_write_file(const char *filepath, int oflag, void *data, size_t size);
int sto_read_file(const char *filepath, void *data, size_t size);

struct sto_srv_dirent *sto_srv_dirent_alloc(const char *name);
void sto_srv_dirent_free(struct sto_srv_dirent *dirent);
int sto_srv_dirent_get_stat(struct sto_srv_dirent *dirent, const char *path);

void sto_srv_dirents_init(struct sto_srv_dirents *dirents);
void sto_srv_dirents_free(struct sto_srv_dirents *dirents);
void sto_srv_dirents_add(struct sto_srv_dirents *dirents, struct sto_srv_dirent *dirent);
void sto_srv_dirents_info_json(struct sto_srv_dirents *dirents, struct spdk_json_write_ctx *w);

#endif /* _STO_SRV_FS_H_ */
