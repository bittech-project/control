#ifndef _STO_HASH_H_
#define _STO_HASH_H_

#include <spdk/queue.h>

struct sto_hash_entry {
	const void *key;
	uint32_t key_len;
	const void *value;

	LIST_ENTRY(sto_hash_entry) list;
};

struct sto_hash {
	uint32_t seed;
	uint32_t nr_of_buckets;
	LIST_HEAD(, sto_hash_entry) buckets[];
};

struct sto_hash *sto_hash_alloc(uint32_t size);
void sto_hash_free(struct sto_hash *ht);

int sto_hash_add(struct sto_hash *ht, const void *key, uint32_t key_len, const void *data);
void *sto_hash_lookup(const struct sto_hash *ht, const void *key, uint32_t key_len);
void sto_hash_del(struct sto_hash *ht, const void *key, uint32_t key_len);

#endif /* _STO_HASH_H_ */
