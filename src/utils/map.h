#ifndef UTILS_MAP_H
#define UTILS_MAP_H

#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "utils/htable.h"

#define DEFAULT_MAP_CAP 8

#define CUSTOM_MAP(name, T, U, hash, compare) \
    struct name { \
        struct htable htable; \
        void* values; \
    }; \
    static inline struct name new_##name##_with_cap(size_t cap) { \
        struct htable htable = new_htable(cap, sizeof(T)); \
        return (struct name) { \
            .htable = htable, \
            .values = xmalloc(sizeof(U) * htable.cap) \
        }; \
    } \
    static inline struct name new_##name(void) { \
        return new_##name##_with_cap(DEFAULT_MAP_CAP); \
    } \
    static inline void free_##name(struct name* map) { \
        free_htable(&map->htable); \
        free(map->values); \
        map->values = NULL; \
    } \
    static inline bool insert_in_##name(struct name* map, T key, U value) { \
        return insert_in_htable( \
            &map->htable, &map->values, \
            &key, sizeof(T), \
            &value, sizeof(U), \
            hash(&key), compare); \
    } \
    static inline U* find_in_##name(struct name* map, T key) { \
        return find_in_htable( \
            &map->htable, map->values, \
            &key, sizeof(T), sizeof(U), \
            hash(&key), compare); \
    } \
    static inline bool remove_from_##name(struct name* map, T key) { \
        return remove_from_htable( \
            &map->htable, map->values, \
            &key, sizeof(T), sizeof(U), \
            hash(&key), compare); \
    } \
    static inline void clear_##name(struct name* map) { \
        clear_htable(&map->htable); \
    }

#define MAP(name, T, U) \
    DEFAULT_HASH(hash_##name##_elem, T) \
    DEFAULT_COMPARE(compare_##name##_elem, T) \
    CUSTOM_MAP(name, T, U, hash_##name##_elem, compare_##name##_elem)

#endif
