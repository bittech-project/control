#include <spdk/log.h>
#include <spdk/likely.h>

#include <rte_jhash.h>

#include "sto_hash.h"

static struct sto_hash_entry *
sto_hash_entry_alloc(const void *key, uint32_t key_len, const void *value)
{
	struct sto_hash_entry *e;

	e = calloc(1, sizeof(*e));
	if (spdk_unlikely(!e)) {
		SPDK_ERRLOG("Failed to alloc hashtable entry\n");
		return NULL;
	}

	e->key = key;
	e->key_len = key_len;
	e->value = value;

	return e;
}

static void
sto_hash_entry_free(struct sto_hash_entry *e)
{
	LIST_REMOVE(e, list);
	free(e);
}

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
	hashtable_size += nr_of_buckets * sizeof(struct sto_hash_entry *);

	ht = calloc(1, hashtable_size);
	if (spdk_unlikely(!ht)) {
		SPDK_ERRLOG("Failed to allocate sto hashtable: size=%u nr_of_buckets=%u hashtable_size=%u\n",
			    size, nr_of_buckets, hashtable_size);
		return NULL;
	}

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
	struct sto_hash_entry *e;
	uint32_t i;

	for (i = 0; i < ht->nr_of_buckets; i++) {
		LIST_FOREACH(e, &ht->buckets[i], list) {
			sto_hash_entry_free(e);
		}
	}

	free(ht);
}

static inline uint32_t
sto_hash_get_bucket_nr(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	uint32_t hash;

	hash = rte_jhash(key, key_len, 0);
	return hash & (ht->nr_of_buckets - 1);
}

int
sto_hash_add(struct sto_hash *ht, const void *key, uint32_t key_len, const void *data)
{
	struct sto_hash_entry *e;
	uint32_t b;

	e = sto_hash_entry_alloc(key, key_len, data);
	if (spdk_unlikely(!e)) {
		SPDK_ERRLOG("Failed to alloc hashtable entry\n");
		return -ENOMEM;
	}

	b = sto_hash_get_bucket_nr(ht, key, key_len);
	LIST_INSERT_HEAD(&ht->buckets[b], e, list);

	return 0;
}

static struct sto_hash_entry *
__sto_hash_lookup(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_entry *e;
	uint32_t b;

	b = sto_hash_get_bucket_nr(ht, key, key_len);

	LIST_FOREACH(e, &ht->buckets[b], list) {
		if (key_len != e->key_len) {
			continue;
		}

		if (!memcmp(key, e->key, key_len)) {
			return e;
		}
	}

	return NULL;
}

void *
sto_hash_lookup(const struct sto_hash *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_entry *e;

	e = __sto_hash_lookup(ht, key, key_len);
	if (!e) {
		return NULL;
	}

	return (void *) e->value;
}

void
sto_hash_del(struct sto_hash *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_entry *e;

	e = __sto_hash_lookup(ht, key, key_len);
	assert(e);

	sto_hash_entry_free(e);
}
