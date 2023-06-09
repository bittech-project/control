#ifndef _STO_INODE_H_
#define _STO_INODE_H_

#include <spdk/util.h>

#include "sto_rpc_readdir.h"

enum sto_inode_type {
	STO_INODE_TYPE_FILE,
	STO_INODE_TYPE_DIR,
	STO_INODE_TYPE_LNK,
	STO_INODE_TYPE_UNSUPPORTED,
	STO_INODE_TYPE_CNT,
};

struct sto_tree_node;
struct sto_inode;

typedef int (*sto_inode_read_t)(struct sto_inode *inode);
typedef int (*sto_inode_read_done_t)(struct sto_inode *inode);
typedef void (*sto_inode_info_json_t)(struct sto_inode *inode, struct spdk_json_write_ctx *w);
typedef void (*sto_inode_destroy_t)(struct sto_inode *inode);

struct sto_inode_ops {
	sto_inode_read_t read;
	sto_inode_read_done_t read_done;
	sto_inode_info_json_t info_json;
	sto_inode_destroy_t destroy;
};

struct sto_inode {
	const char *name;
	char *path;

	enum sto_inode_type type;
	uint32_t mode;

	struct sto_inode_ops *ops;
	struct sto_tree_node *node;
};

static inline bool
sto_inode_read_only(struct sto_inode *inode)
{
	return !(inode->mode & S_IWUSR);
}

static inline bool
sto_inode_write_only(struct sto_inode *inode)
{
	return !(inode->mode & S_IRUSR);
}

struct sto_file_inode {
	struct sto_inode inode;
	char *buf;
};

static inline struct sto_file_inode *
sto_file_inode(struct sto_inode *inode)
{
	return SPDK_CONTAINEROF(inode, struct sto_file_inode, inode);
}

static inline char *
sto_file_inode_buf(struct sto_inode *inode)
{
	struct sto_file_inode *file_inode = sto_file_inode(inode);
	return file_inode->buf;
}

struct sto_dir_inode {
	struct sto_inode inode;
	struct sto_dirents dirents;
};

static inline struct sto_dir_inode *
sto_dir_inode(struct sto_inode *inode)
{
	return SPDK_CONTAINEROF(inode, struct sto_dir_inode, inode);
}

struct sto_inode *sto_inode_create(const char *name, const char *path,
				   uint32_t mode, ...);
void sto_inode_read(struct sto_inode *inode);

#endif /* _STO_INODE_H_ */
