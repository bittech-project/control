#include <spdk/log.h>
#include <spdk/likely.h>
#include <spdk/util.h>

#include <rte_jhash.h>

#include "sto_hash.h"

static inline uint32_t
fls(uint32_t x)
{
	uint32_t position;
	uint32_t i;

	if (x == 0)
		return 0;

	for (i = (x >> 1), position = 0; i != 0; ++position)
		i >>= 1;

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

	if (val >= STO_HASH_MAX_BUCKETS)
		return STO_HASH_MAX_BUCKETS;

	return roundup_pow_of_two(val);
}

struct sto_hash *
sto_hash_alloc(uint32_t size)
{
	struct sto_hash *ht;
	uint32_t nr_of_buckets, hashtable_size;
	uint32_t i;

	nr_of_buckets = sto_hash_buckets(size);

	hashtable_size = sizeof(struct sto_hash);
	hashtable_size += nr_of_buckets * sizeof(struct sto_hash_elem *);

	ht = calloc(1, hashtable_size);
	if (spdk_unlikely(!ht)) {
		SPDK_ERRLOG("Failed to allocate sto hashtable: size=%u nr_of_buckets=%u hashtable_size=%u\n",
			    size, nr_of_buckets, hashtable_size);
		return NULL;
	}

	ht->seed = 0;
	ht->nr_of_buckets = nr_of_buckets;

	for (i = 0; i < ht->nr_of_buckets; i++) {
		LIST_INIT(&ht->buckets[i]);
	}

	SPDK_NOTICELOG("Create STO hash table: size=%u, nr_of_buckets=%u\n",
		       size, nr_of_buckets);

	return ht;
}

void
sto_hash_free(struct sto_hash *ht)
{
	if (!sto_hash_empty(ht)) {
		SPDK_ERRLOG("STO hashtable is not empty!!!\n");
	}

	free(ht);
}

static inline uint32_t
sto_hash_get_bucket_nr(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	uint32_t hash;

	hash = rte_jhash(key, key_len, ht->seed);
	return hash & (ht->nr_of_buckets - 1);
}

void
sto_hash_add_elem(struct sto_hash *ht, struct sto_hash_elem *he)
{
	uint32_t b;

	b = sto_hash_get_bucket_nr(ht, he->key, he->key_len);
	LIST_INSERT_HEAD(&ht->buckets[b], he, list);
}

struct sto_hash_elem *
sto_hash_lookup_elem(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_elem *he;
	uint32_t b;

	b = sto_hash_get_bucket_nr(ht, key, key_len);

	LIST_FOREACH(he, &ht->buckets[b], list) {
		if (key_len != he->key_len) {
			continue;
		}

		if (!memcmp(key, he->key, key_len)) {
			return he;
		}
	}

	return NULL;
}

void
sto_hash_remove_elem(struct sto_hash_elem *he)
{
	LIST_REMOVE(he, list);
}

struct sto_hash_entry {
	const void *value;
	struct sto_hash_elem he;
};

#define STO_HASH_ENTRY(x) \
	SPDK_CONTAINEROF((x), struct sto_hash_entry, he)

static inline int
sto_hash_add_entry(struct sto_hash *ht, const void *key, uint32_t key_len, const void *value)
{
	struct sto_hash_entry *entry;

	entry = calloc(1, sizeof(*entry));
	if (spdk_unlikely(!entry)) {
		SPDK_ERRLOG("Failed to alloc hashtable entry\n");
		return -ENOMEM;
	}

	sto_hash_elem_init(&entry->he, key, key_len);
	entry->value = value;

	sto_hash_add_elem(ht, &entry->he);

	return 0;
}

static inline void
sto_hash_remove_entry(struct sto_hash_entry *entry)
{
	sto_hash_remove_elem(&entry->he);
	free(entry);
}

static inline struct sto_hash_entry *
sto_hash_lookup_entry(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_elem *he;

	he = sto_hash_lookup_elem(ht, key, key_len);
	if (!he) {
		return NULL;
	}

	return STO_HASH_ENTRY(he);
}

int
sto_hash_add(struct sto_hash *ht, const void *key, uint32_t key_len, const void *value)
{
	return sto_hash_add_entry(ht, key, key_len, value);
}

void *
sto_hash_lookup(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_entry *entry;

	entry = sto_hash_lookup_entry(ht, key, key_len);
	if (!entry) {
		return NULL;
	}

	return (void *) entry->value;
}

void
sto_hash_remove(struct sto_hash *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_entry *entry;

	entry = sto_hash_lookup_entry(ht, key, key_len);
	assert(entry);

	sto_hash_remove_entry(entry);
}

void
sto_hash_clear(struct sto_hash *ht)
{
	struct sto_hash_elem *he, *tmp;
	uint32_t i;

	for (i = 0; i < ht->nr_of_buckets; i++) {
		LIST_FOREACH_SAFE(he, &ht->buckets[i], list, tmp) {
			sto_hash_remove_entry(STO_HASH_ENTRY(he));
		}
	}
}
