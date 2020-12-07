#ifndef UTILS_HTABLE_H
#define UTILS_HTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/primes.h"
#include "utils/hash.h"

#define MAX_LOAD_FACTOR 70//%

#define DEFAULT_HASH(name, T) \
    static inline uint32_t name(const void* key) { \
        return hash_bytes(FNV_OFFSET, key, sizeof(T)); \
    }

#define DEFAULT_COMPARE(name, T) \
    static inline bool name(const void* left, const void* right) { \
        return !memcmp(left, right, sizeof(T)); \
    }

struct htable {
    size_t cap;
    size_t size;
    uint32_t* hashes;
    void* keys;
};

struct htable new_htable(size_t, size_t);
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
