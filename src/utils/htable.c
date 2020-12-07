#include <string.h>
#include <assert.h>

#include "utils/htable.h"
#include "utils/utils.h"

#define HASH_MASK UINT32_C(0x7FFFFFFF)

static inline bool is_bucket_used(const struct htable* htable, size_t index) {
    return (htable->hashes[index] & ~HASH_MASK) != 0;
}

static inline size_t increment_wrap(const struct htable* htable, size_t index) {
    return index + 1 >= htable->cap ? 0 : index + 1;
}

static inline bool needs_rehash(const struct htable* htable) {
    return htable->size * 100 > htable->cap * MAX_LOAD_FACTOR;
}

struct htable new_htable(size_t cap, size_t key_size) {
    cap = next_prime(cap);
    return (struct htable) {
        .cap    = cap,
        .size   = 0,
        .keys   = xmalloc(key_size * cap),
        .hashes = xcalloc(cap, sizeof(uint32_t))
    };
}

void free_htable(struct htable* htable) {
    free(htable->keys);
    free(htable->hashes);
    htable->keys = NULL;
    htable->hashes = NULL;
    htable->size = htable->cap = 0;
}

void rehash_htable(struct htable* htable, void** values, size_t key_size, size_t value_size) {
    size_t new_cap = next_prime(htable->cap);
    if (new_cap <= htable->cap)
        new_cap = htable->cap * 2 - 1;
    void* new_keys       = xmalloc(key_size * new_cap);
    uint32_t* new_hashes = xcalloc(new_cap, sizeof(uint32_t));
    void* new_values     = xmalloc(value_size * new_cap);
    for (size_t i = 0, n = htable->cap; i < n; ++i) {
        if (!is_bucket_used(htable, i))
            continue;
        const void* key   = ((char*)htable->keys) + key_size * i;
        const void* value = ((char*)*values) + value_size * i;
        uint32_t hash = htable->hashes[i];
        size_t index = mod_prime(hash, new_cap);
        while (is_bucket_used(htable, index))
            index = increment_wrap(htable, index);
        memcpy(((char*)new_keys) + key_size * index, key, key_size);
        memcpy(((char*)new_values) + value_size * index, value, value_size);
        new_hashes[index] = hash;
    }
    free(htable->keys);
    free(*values);
    htable->keys   = new_keys;
    htable->hashes = new_hashes;
    htable->cap    = new_cap;
    *values        = new_values;
}

bool insert_in_htable(
    struct htable* htable, void** values,
    const void* key, size_t key_size,
    const void* value, size_t value_size,
    uint32_t hash, bool (*compare)(const void*, const void*)) {
    size_t index = mod_prime(hash, htable->cap);
    while (is_bucket_used(htable, index)) {
        if (htable->hashes[index] == hash &&
            compare(((char*)htable->keys) + key_size * index, key))
            return false;
        index = increment_wrap(htable, index);
    }
    memcpy(((char*)htable->keys) + key_size * index, key, key_size);
    memcpy(((char*)*values) + value_size * index, value, value_size);
    htable->size++;
    if (needs_rehash(htable))
        rehash_htable(htable, values, key_size, value_size);
    return true;
}

void* find_in_htable(
    const struct htable* htable, void* values,
    const void* key, size_t key_size, size_t value_size,
    uint32_t hash, bool (*compare)(const void*, const void*)) {
    size_t index = mod_prime(hash, htable->cap);
    while (is_bucket_used(htable, index)) {
        if (htable->hashes[index] == hash &&
            compare(((char*)htable->keys) + key_size * index, key))
            return ((char*)values) + value_size * index;
        index = increment_wrap(htable, index);
    }
    return NULL;
}

bool remove_from_htable(
    struct htable* htable, void* values,
    const void* target_key, size_t key_size, size_t value_size,
    uint32_t hash, bool (*compare)(const void*, const void*)) {
    void* key = find_in_htable(
        htable, htable->keys,
        target_key, key_size, key_size,
        hash, compare);
    if (!key)
        return false;
    size_t index = ((char*)key - (char*)htable->keys) / key_size;
    size_t next_index = increment_wrap(htable, index);
    void* value = ((char*)values) + value_size * index;
    while (is_bucket_used(htable, next_index)) {
        size_t desired_index = mod_prime(htable->hashes[next_index], htable->cap);
        if (next_index == desired_index)
            break;
        void* next_key   = ((char*)htable->keys) + key_size * next_index;
        void* next_value = ((char*)values) + value_size * next_index;
        memcpy(key, next_key, key_size);
        memcpy(value, next_value, value_size);
        key   = next_key;
        value = next_value;
        index = next_index;
        next_index = increment_wrap(htable, next_index);
    }
    htable->hashes[index] = 0;
    return true;
}

void clear_htable(struct htable* htable) {
    memset(htable->hashes, 0, sizeof(uint32_t) * htable->cap);
}
