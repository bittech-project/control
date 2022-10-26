#ifndef _STO_FSTAT_FRONT_H_
#define _STO_FSTAT_FRONT_H_

struct sto_fstat_req;
typedef void (*fstat_done_t)(struct sto_fstat_req *req);

struct sto_stat {
	uint64_t dev;		/* ID of device containing file */
	uint64_t ino;		/* inode number */
	uint32_t mode;		/* protection */
	uint64_t nlink;		/* number of hard links */
	uint64_t uid;		/* user ID of owner */
	uint64_t gid;		/* group ID of owner */
	uint64_t rdev;		/* device ID (if special file) */
	uint64_t size;		/* total size, in bytes */
	uint64_t blksize;	/* blocksize for file system I/O */
	uint64_t blocks;	/* number of 512B blocks allocated */
	uint64_t atime;		/* time of last access */
	uint64_t mtime;		/* time of last modification */
	uint64_t ctime;		/* time of last status change */
};

struct sto_fstat_ctx {
	struct sto_stat *stat;

	void *priv;
	fstat_done_t fstat_done;
};

struct sto_fstat_req {
	struct {
		const char *filename;
	};

	int returncode;
	struct sto_stat *stat;

	void *priv;
	fstat_done_t fstat_done;
};

int sto_fstat(const char *filename, struct sto_fstat_ctx *ctx);
void sto_fstat_free(struct sto_fstat_req *req);

#endif /* _STO_FSTAT_FRONT_H_ */
