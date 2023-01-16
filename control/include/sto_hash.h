#ifndef _STO_HASH_H_
#define _STO_HASH_H_

#include <spdk/queue.h>

struct sto_hash_elem {
	LIST_ENTRY(sto_hash_elem) list;

	const void *key;
	const void *value;
	uint32_t key_len;
};

struct sto_hash {
	uint32_t seed;
	uint32_t nr_of_buckets;
	LIST_HEAD(, sto_hash_elem) buckets[];
};

struct sto_hash *sto_hash_alloc(uint32_t size);
void sto_hash_free(struct sto_hash *ht);

static inline bool
sto_hash_empty(struct sto_hash *ht)
{
	uint32_t i;

	for (i = 0; i < ht->nr_of_buckets; i++) {
		if (!LIST_EMPTY(&ht->buckets[i])) {
			return false;
		}
	}

	return true;
}

static inline void
sto_hash_elem_init(struct sto_hash_elem *he, const void *key, uint32_t key_len, const void *value)
{
	he->key = key;
	he->key_len = key_len;
	he->value = value;
}

void sto_hash_add_elem(struct sto_hash *ht, struct sto_hash_elem *he);
void sto_hash_remove_elem(struct sto_hash_elem *he);

void *sto_hash_lookup(const struct sto_hash *ht, const void *key, uint32_t key_len);

int sto_hash_add(struct sto_hash *ht, const void *key, uint32_t key_len, const void *data);
void sto_hash_remove(struct sto_hash *ht, const void *key, uint32_t key_len);
void sto_hash_clear(struct sto_hash *ht);

static inline void
sto_hash_clear_and_free(struct sto_hash *ht)
{
	sto_hash_clear(ht);
	sto_hash_free(ht);
}

#endif /* _STO_HASH_H_ */
