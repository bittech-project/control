#include <spdk/log.h>
#include <spdk/likely.h>

#include <rte_jhash.h>

#include "sto_hashtable.h"

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

static inline uint32_t fls(uint32_t x)
{
	uint32_t position;
	uint32_t i;

	if (x == 0)
		return 0;

	for (i = (x >> 1), position = 0; i != 0; ++position)
		i >>= 1;

	return position + 1;
}

static inline uint32_t roundup_pow_of_two(uint32_t x)
{
	return 1UL << fls(x - 1);
}

struct sto_hashtable *
sto_hashtable_alloc(uint32_t size)
{
	struct sto_hashtable *ht;
	uint32_t nr_of_buckets, hashtable_size;
	uint32_t i;

	nr_of_buckets = roundup_pow_of_two(size);

	hashtable_size = sizeof(struct sto_hashtable);
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

	return ht;
}

void
sto_hashtable_free(struct sto_hashtable *ht)
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

int
sto_hashtable_add(struct sto_hashtable *ht, const void *key, uint32_t key_len, const void *data)
{
	struct sto_hash_entry *e;
	uint32_t hash, b;

	e = sto_hash_entry_alloc(key, key_len, data);
	if (spdk_unlikely(!e)) {
		SPDK_ERRLOG("Failed to alloc hashtable entry\n");
		return -ENOMEM;
	}

	hash = rte_jhash(key, key_len, 0);
	b = hash & (ht->nr_of_buckets - 1);

	LIST_INSERT_HEAD(&ht->buckets[b], e, list);

	return 0;
}

static struct sto_hash_entry *
__sto_hashtable_lookup(const struct sto_hashtable *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_entry *e;
	uint32_t hash, b;

	hash = rte_jhash(key, key_len, 0);
	b = hash & (ht->nr_of_buckets - 1);

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
sto_hashtable_lookup(const struct sto_hashtable *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_entry *e;

	e = __sto_hashtable_lookup(ht, key, key_len);
	if (!e) {
		return NULL;
	}

	return (void *) e->value;
}

void
sto_hashtable_del(struct sto_hashtable *ht, const void *key, uint32_t key_len)
{
	struct sto_hash_entry *e;

	e = __sto_hashtable_lookup(ht, key, key_len);
	assert(e);

	sto_hash_entry_free(e);
}
