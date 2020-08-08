#include <string.h>
#include <assert.h>
#include "htable.h"
#include "utils.h"

#define HASH_MASK UINT32_C(0x7FFFFFFF)

struct htable new_htable(size_t elem_size, size_t elem_cap, cmpfn_t cmp) {
    elem_cap = round_to_pow2(elem_cap);
    return (struct htable) {
        .elems     = xmalloc(elem_size * elem_cap),
        .hashes    = xcalloc(sizeof(uint32_t), elem_cap),
        .elem_size = elem_size,
        .elem_cap  = elem_cap,
        .cmp       = cmp
    };
}

void free_htable(struct htable* htable) {
    free(htable->elems);
    free(htable->hashes);
}

static inline bool is_deleted(uint32_t h) {
    return (h & ~HASH_MASK) == 0;
}

static inline void* elem_at(struct htable* htable, size_t index) {
    return (char*)htable->elems + index * htable->elem_size;
}

static void rehash(struct htable* htable) {
    size_t new_cap = htable->elem_cap * 2;
    struct htable new_htable = {
        .elems      = xmalloc(htable->elem_size * new_cap),
        .hashes     = xcalloc(htable->elem_size, new_cap),
        .elem_cap   = new_cap,
        .elem_size  = htable->elem_size,
        .elem_count = 0,
        .cmp        = htable->cmp
    };
    for (size_t i = 0; i < htable->elem_cap; ++i) {
        uint32_t hash = htable->hashes[i];
        if (is_deleted(hash))
            continue;
        insert_in_htable(&new_htable, elem_at(htable, i), hash, NULL);
    }
    *htable = new_htable;
}

static inline size_t hash_to_index(const struct htable* htable, uint32_t hash) {
    return (hash & HASH_MASK) & (htable->elem_cap - 1);
}

static inline size_t lookup_distance(const struct htable* htable, size_t from, size_t to) {
    return to >= from ? to - from : htable->elem_cap - from + to;
}

static inline bool needs_rehash(const struct htable* htable) {
    return htable->elem_count > 8 * htable->elem_cap / 10;
}

bool insert_in_htable(struct htable* htable, void* elem, uint32_t hash, void** res) {
    if (needs_rehash(htable))
        rehash(htable);
    assert(!needs_rehash(htable));

    size_t index = hash_to_index(htable, hash);
    size_t dist = 0;
    while (true) {
        uint32_t old_hash = htable->hashes[index];
        void* old_elem = elem_at(htable, index);

        // If this bucket is free, use it
        if (is_deleted(old_hash)) {
            memcpy(old_elem, elem, htable->elem_size);
            htable->hashes[index] = (hash & HASH_MASK) | ~HASH_MASK;
            htable->elem_count++;
            if (res) *res = old_elem;
            return true;
        }

        // Test if the element in the hash table at this position
        // is equal to the inserted element, and if so, exit.
        if (((old_hash ^ hash) & HASH_MASK) == 0 && htable->cmp(elem, old_elem)) {
            if (res) *res = old_elem;
            return false;
        }

        // Robin-hood hashing: either continue inserting the current element,
        // or swap the current element for the one located in this bucket,
        // depending on the current distance to the initial bucket.
        size_t old_index = hash_to_index(htable, old_hash);
        size_t old_dist  = lookup_distance(htable, old_index, index);
        if (dist > old_dist) {
            memswp(old_elem, elem, htable->elem_size);
            htable->hashes[index] = (hash & HASH_MASK) | ~HASH_MASK;
            dist = old_dist;
            hash = old_hash;
        }

        index = (index + 1) & (htable->elem_cap - 1);
        dist++;
    }
}

void* find_in_htable(struct htable* htable, const void* elem, uint32_t hash) {
    size_t index = hash_to_index(htable, hash);
    size_t dist = 0;
    while (true) {
        uint32_t old_hash = htable->hashes[index];
        void* old_elem = elem_at(htable, index);
        if (is_deleted(old_hash))
            return NULL;

        size_t old_index = hash_to_index(htable, old_hash);
        size_t old_dist = lookup_distance(htable, old_index, index);
        if (dist > old_dist)
            return NULL;

        if (((old_hash ^ hash) & HASH_MASK) == 0 && htable->cmp(elem, old_elem))
            return old_elem;

        index = (index + 1) & (htable->elem_cap - 1);
        dist++;
    }
}
