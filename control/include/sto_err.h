#ifndef _STO_ERR_H_
#define _STO_ERR_H_

#include <spdk/likely.h>

#include "sto_compiler.h"

/*
 * Kernel pointers have redundant information, so we can use a
 * scheme where we can return either an error code or a dentry
 * pointer with the same return value.
 *
 * This should be a per-architecture thing, to allow different
 * error and pointer decisions.
 */
#define MAX_ERRNO	4095

#define IS_ERR_VALUE(x) spdk_unlikely((uintptr_t)(void *) (x) >= (uintptr_t) - MAX_ERRNO)

static inline void * __must_check
ERR_PTR(uintptr_t error)
{
	return (void *) error;
}

static inline uintptr_t __must_check
PTR_ERR(const void *ptr)
{
	return (uintptr_t) ptr;
}

static inline bool __must_check
IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE((uintptr_t) ptr);
}

static inline bool __must_check
IS_ERR_OR_NULL(const void *ptr)
{
	return spdk_unlikely(!ptr) || IS_ERR_VALUE((uintptr_t) ptr);
}

static inline int __must_check
PTR_ERR_OR_ZERO(const void *ptr)
{
	if (IS_ERR(ptr)) {
		return PTR_ERR(ptr);
	}

	return 0;
}

/**
 * ERR_CAST - Explicitly cast an error-valued pointer to another pointer type
 * @ptr: The pointer to cast.
 *
 * Explicitly cast an error-valued pointer to another pointer type in such a
 * way as to make it clear that's what's going on.
 */
static inline void * __must_check
ERR_CAST(const void *ptr)
{
	/* cast away the const */
	return (void *) ptr;
}

#endif /* _STO_ERR_H_ */
