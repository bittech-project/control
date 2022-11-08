#ifndef _STO_READDIR_FRONT_H_
#define _STO_READDIR_FRONT_H_

struct spdk_json_write_ctx;

typedef void (*readdir_done_t)(void *priv);

struct sto_dirent {
	char *name;
	uint32_t mode;
};

#define STO_DIRENT_MAX_CNT 256

struct sto_dirents {
	struct sto_dirent dirents[STO_DIRENT_MAX_CNT];
	size_t cnt;
};

struct sto_dirents_json_cfg {
	const char *name;
	const char **exclude_list;
	uint32_t type;
};

struct sto_readdir_result {
	int returncode;
	struct sto_dirents dirents;
};

struct sto_readdir_args {
	void *priv;
	readdir_done_t readdir_done;

	struct sto_readdir_result *result;
};

struct sto_readdir_req {
	struct {
		bool skip_hidden;
		const char *dirpath;
	};

	struct sto_readdir_result *result;

	void *priv;
	readdir_done_t readdir_done;
};

void sto_readdir_result_free(struct sto_readdir_result *result);

int sto_readdir(const char *dirpath, struct sto_readdir_args *args);

void sto_dirents_info_json(struct sto_dirents *dirents,
			   struct sto_dirents_json_cfg *cfg, struct spdk_json_write_ctx *w);

int sto_dirent_copy(struct sto_dirent *src, struct sto_dirent *dst);
void sto_dirent_free(struct sto_dirent *dirent);

#endif /* _STO_READDIR_FRONT_H_ */
