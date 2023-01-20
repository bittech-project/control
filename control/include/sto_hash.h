#ifndef _STO_HASH_H_
#define _STO_HASH_H_

#include <spdk/queue.h>
#include <spdk/util.h>

struct sto_hash_elem {
	LIST_ENTRY(sto_hash_elem) list;

	const void *key;
	uint32_t key_len;
};

struct sto_bucket_table;

struct sto_hash {
	uint32_t seed;
	struct sto_bucket_table *tbl;
};

struct sto_hash_iter {
	struct sto_bucket_table *tbl;
	uint32_t slot;
	struct sto_hash_elem *he;
};

struct sto_shash {
	struct sto_hash ht;
};

struct sto_shash_iter {
	struct sto_hash_iter iter;
};

static inline void
sto_hash_elem_init(struct sto_hash_elem *he, const void *key, uint32_t key_len)
{
	he->key = key;
	he->key_len = key_len;
}

static inline void
sto_hash_elem_del(struct sto_hash_elem *he)
{
	LIST_REMOVE(he, list);
}

int sto_hash_init(struct sto_hash *ht, uint32_t size);
void sto_hash_destroy(struct sto_hash *ht);
bool sto_hash_empty(struct sto_hash *ht);

void sto_hash_add(struct sto_hash *ht, struct sto_hash_elem *he);
struct sto_hash_elem *sto_hash_lookup(const struct sto_hash *ht, const void *key, uint32_t key_len);

static inline void
sto_hash_iter_init(struct sto_hash_iter *iter, const struct sto_hash *ht)
{
	iter->tbl = ht->tbl;
	iter->slot = 0;
	iter->he = NULL;
}

struct sto_hash_elem *sto_hash_iter_next(struct sto_hash_iter *iter);

static inline int
sto_shash_init(struct sto_shash *sht, uint32_t size)
{
	return sto_hash_init(&sht->ht, size);
}

void sto_shash_clear(struct sto_shash *sht);

static inline void
sto_shash_destroy(struct sto_shash *sht)
{
	sto_shash_clear(sht);
	sto_hash_destroy(&sht->ht);
}

int sto_shash_add(struct sto_shash *sht, const void *key, uint32_t key_len, const void *data);
void *sto_shash_lookup(const struct sto_shash *sht, const void *key, uint32_t key_len);
void sto_shash_remove(struct sto_shash *sht, const void *key, uint32_t key_len);

static inline void
sto_shash_iter_init(struct sto_shash_iter *iter, const struct sto_shash *sht)
{
	sto_hash_iter_init(&iter->iter, &sht->ht);
}

void *sto_shash_iter_next(struct sto_shash_iter *iter);

#endif /* _STO_HASH_H_ */
