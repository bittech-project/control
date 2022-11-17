#ifndef _STO_RPC_READDIR_H_
#define _STO_RPC_READDIR_H_

struct spdk_json_write_ctx;

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

typedef void (*sto_rpc_readdir_done_t)(void *priv, int rc);

struct sto_rpc_readdir_args {
	void *priv;
	sto_rpc_readdir_done_t done;

	struct sto_dirents *dirents;
};

int sto_rpc_readdir(const char *dirpath, struct sto_rpc_readdir_args *args);

void sto_dirents_info_json(struct sto_dirents *dirents,
			   struct sto_dirents_json_cfg *cfg, struct spdk_json_write_ctx *w);
void sto_dirents_free(struct sto_dirents *dirents);

int sto_dirent_copy(struct sto_dirent *src, struct sto_dirent *dst);
void sto_dirent_free(struct sto_dirent *dirent);

#endif /* _STO_RPC_READDIR_H_ */
