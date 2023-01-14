#ifndef _STO_HASHTABLE_H_
#define _STO_HASHTABLE_H_

#include <spdk/queue.h>

struct sto_hash_entry {
	const void *key;
	uint32_t key_len;
	const void *value;

	LIST_ENTRY(sto_hash_entry) list;
};

struct sto_hashtable {
	uint32_t nr_of_buckets;
	LIST_HEAD(, sto_hash_entry) buckets[];
};

struct sto_hashtable *sto_hashtable_alloc(uint32_t size);
void sto_hashtable_free(struct sto_hashtable *ht);

int sto_hashtable_add(struct sto_hashtable *ht, const void *key, uint32_t key_len, const void *data);
void *sto_hashtable_lookup(const struct sto_hashtable *ht, const void *key, uint32_t key_len);
void sto_hashtable_del(struct sto_hashtable *ht, const void *key, uint32_t key_len);

#endif /* _STO_HASHTABLE_H_ */
