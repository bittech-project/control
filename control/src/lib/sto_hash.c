#include "sto_hash.h"

#include <spdk/stdinc.h>
#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>
#include <spdk/queue.h>

#include <rte_jhash.h>

struct sto_bucket_table {
	uint32_t nr_of_buckets;

	LIST_HEAD(, sto_hash_elem) buckets[];
};

static inline uint32_t
fls(uint32_t x)
{
	uint32_t position;
	uint32_t i;

	if (x == 0) {
		return 0;
	}

	for (i = (x >> 1), position = 0; i != 0; ++position) {
		i >>= 1;
	}

	return position + 1;
}

static inline uint32_t
roundup_pow_of_two(uint32_t x)
{
	return 1UL << fls(x - 1);
}

/* Number of buckets is stored in uint32_t, so cap our result to 1U << 31 */
#define STO_HASH_MAX_BUCKETS (1U << 31)

static uint32_t
sto_hash_buckets(uint32_t size)
{
	uint64_t val = ((uint64_t) size * 4) / 3;

	if (val >= STO_HASH_MAX_BUCKETS) {
		return STO_HASH_MAX_BUCKETS;
	}

	return roundup_pow_of_two(val);
}

static struct sto_bucket_table *
bucket_table_alloc(uint32_t size)
{
	struct sto_bucket_table *tbl;
	uint32_t nr_of_buckets, table_size;
	uint32_t i;

	nr_of_buckets = sto_hash_buckets(size);

	table_size = sizeof(struct sto_bucket_table);
	table_size += nr_of_buckets * sizeof(struct sto_hash_elem *);

	tbl = calloc(1, table_size);
	if (spdk_unlikely(!tbl)) {
		SPDK_ERRLOG("Failed to allocate %u buckets: table_size=%u\n",
			    nr_of_buckets, table_size);
		return NULL;
	}

	tbl->nr_of_buckets = nr_of_buckets;

	for (i = 0; i < tbl->nr_of_buckets; i++) {
		LIST_INIT(&tbl->buckets[i]);
	}

	return tbl;
}

static void
bucket_table_free(struct sto_bucket_table *tbl)
{
	free(tbl);
}

int
sto_hash_init(struct sto_hash *ht, uint32_t size)
{
	ht->seed = 0;

	ht->tbl = bucket_table_alloc(size);
	if (spdk_unlikely(!ht->tbl)) {
		SPDK_ERRLOG("Failed to allocate bucket table for size=%u\n", size);
		return -ENOMEM;
	}

	return 0;
}

void
sto_hash_destroy(struct sto_hash *ht)
{
	if (!sto_hash_empty(ht)) {
		SPDK_ERRLOG("STO hashtable is not empty!!!\n");
	}

	bucket_table_free(ht->tbl);
}

bool
sto_hash_empty(struct sto_hash *ht)
{
	struct sto_bucket_table *tbl = ht->tbl;
	uint32_t i;

	for (i = 0; i < tbl->nr_of_buckets; i++) {
		if (!LIST_EMPTY(&tbl->buckets[i])) {
			return false;
		}
	}

	return true;
}

static inline uint32_t
sto_hash_get_bucket_nr(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	uint32_t hash;

	hash = rte_jhash(key, key_len, ht->seed);
	return hash & (ht->tbl->nr_of_buckets - 1);
}

void
sto_hash_add(struct sto_hash *ht, struct sto_hash_elem *he)
{
	uint32_t b;

	b = sto_hash_get_bucket_nr(ht, he->key, he->key_len);
	LIST_INSERT_HEAD(&ht->tbl->buckets[b], he, list);
}

struct sto_hash_elem *
sto_hash_lookup(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	struct sto_bucket_table *tbl = ht->tbl;
	struct sto_hash_elem *he;
	uint32_t b;

	b = sto_hash_get_bucket_nr(ht, key, key_len);

	LIST_FOREACH(he, &tbl->buckets[b], list) {
		if (key_len != he->key_len) {
			continue;
		}

		if (!memcmp(key, he->key, key_len)) {
			return he;
		}
	}

	return NULL;
}

struct sto_hash_elem *
sto_hash_iter_next(struct sto_hash_iter *iter)
{
	struct sto_bucket_table *tbl = iter->tbl;
	struct sto_hash_elem *he = iter->he;

	for (; iter->slot < tbl->nr_of_buckets; iter->slot++) {
		he = !he ? LIST_FIRST(&tbl->buckets[iter->slot]) : LIST_NEXT(he, list);
		if (he) {
			return iter->he = he;
		}
	}

	return NULL;
}

struct sto_shash_entry {
	const void *value;
	struct sto_hash_elem he;
};

#define STO_SHASH_ENTRY(x) \
	SPDK_CONTAINEROF((x), struct sto_shash_entry, he)

static inline int
sto_shash_add_entry(struct sto_shash *sht, const void *key, uint32_t key_len, const void *value)
{
	struct sto_shash_entry *entry;

	entry = calloc(1, sizeof(*entry));
	if (spdk_unlikely(!entry)) {
		SPDK_ERRLOG("Failed to alloc hashtable entry\n");
		return -ENOMEM;
	}

	sto_hash_elem_init(&entry->he, key, key_len);
	entry->value = value;

	sto_hash_add(&sht->ht, &entry->he);

	return 0;
}

static inline void
sto_shash_remove_entry(struct sto_shash_entry *entry)
{
	sto_hash_elem_del(&entry->he);
	free(entry);
}

static inline struct sto_shash_entry *
sto_shash_lookup_entry(const struct sto_shash *sht, const void *key, uint32_t key_len)
{
	struct sto_hash_elem *he;

	he = sto_hash_lookup(&sht->ht, key, key_len);
	if (!he) {
		return NULL;
	}

	return STO_SHASH_ENTRY(he);
}

static inline struct sto_shash_entry *
sto_shash_next_entry(struct sto_shash_iter *iter)
{
	struct sto_hash_elem *he;

	he = sto_hash_iter_next(&iter->iter);
	if (!he) {
		return NULL;
	}

	return STO_SHASH_ENTRY(he);
}

int
sto_shash_add(struct sto_shash *sht, const void *key, uint32_t key_len, const void *value)
{
	return sto_shash_add_entry(sht, key, key_len, value);
}

void *
sto_shash_lookup(const struct sto_shash *sht, const void *key, uint32_t key_len)
{
	struct sto_shash_entry *entry;

	entry = sto_shash_lookup_entry(sht, key, key_len);
	if (!entry) {
		return NULL;
	}

	return (void *) entry->value;
}

void
sto_shash_remove(struct sto_shash *sht, const void *key, uint32_t key_len)
{
	struct sto_shash_entry *entry;

	entry = sto_shash_lookup_entry(sht, key, key_len);
	assert(entry);

	sto_shash_remove_entry(entry);
}

void
sto_shash_clear(struct sto_shash *sht)
{
	struct sto_bucket_table *tbl = sht->ht.tbl;
	struct sto_hash_elem *he, *tmp;
	uint32_t i;

	for (i = 0; i < tbl->nr_of_buckets; i++) {
		LIST_FOREACH_SAFE(he, &tbl->buckets[i], list, tmp) {
			sto_shash_remove_entry(STO_SHASH_ENTRY(he));
		}
	}
}

void *
sto_shash_iter_next(struct sto_shash_iter *iter)
{
	struct sto_shash_entry *entry;

	entry = sto_shash_next_entry(iter);
	if (!entry) {
		return NULL;
	}

	return (void *) entry->value;
}
