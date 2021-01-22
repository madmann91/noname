#ifndef UTILS_HTABLE_H
#define UTILS_HTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/primes.h"
#include "utils/hash.h"

/*
 * This table only uses the lower 31 bits of the hash value.
 * The highest bit is used to encode buckets that are used.
 * Hashes are stored in the hash map to speed up comparisons:
 * The hash value is compared with the bucket's hash value first,
 * and the comparison function is only used if they compare equal.
 * The collision resolution strategy is linear probing.
 */

#define HASH_MASK UINT32_C(0x7FFFFFFF)
#define MAX_LOAD_FACTOR 70//%

#define DEFAULT_HASH(name, T) \
    static inline uint32_t name(const void* key) { \
        return hash_bytes(hash_init(), key, sizeof(T)); \
    }

#define DEFAULT_COMPARE(name, T) \
    static inline bool name(const void* left, const void* right) { \
        return !memcmp(left, right, sizeof(T)); \
    }

#define FORALL_IN_HTABLE(htable, i, ...) \
    for (size_t i = 0, n = (htable)->cap; i < n; ++i) { \
        if ((htable)->hashes[i] & ~HASH_MASK) { \
            __VA_ARGS__ \
        } \
    }

struct htable {
    size_t cap;
    size_t size;
    uint32_t* hashes;
    void* keys;
};

struct htable new_htable(size_t, size_t);
struct htable new_htable_on_stack(size_t, void*, uint32_t*);
void free_htable(struct htable*);
void rehash_htable(struct htable*, void**, size_t, size_t);
bool insert_in_htable(
    struct htable*, void**,
    const void*, size_t,
    const void*, size_t, uint32_t,
    bool (*)(const void*, const void*));
void* find_in_htable(
    const struct htable*, void*,
    const void*, size_t, size_t, uint32_t,
    bool (*)(const void*, const void*));
bool remove_from_htable(
    struct htable*, void*,
    const void*, size_t, size_t, uint32_t,
    bool (*)(const void*, const void*));
void clear_htable(struct htable*);

#endif
