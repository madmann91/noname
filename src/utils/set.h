#ifndef UTILS_SET_H
#define UTILS_SET_H

#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "utils/htable.h"

#define DEFAULT_SET_CAP 8

#define CUSTOM_SET(name, T, hash, compare) \
    struct name { \
        struct htable htable; \
    }; \
    static inline struct name new_##name##_with_cap(size_t cap) { \
        return (struct name) { \
            .htable = new_htable(cap, sizeof(T)), \
        }; \
    } \
    static inline struct name new_##name(void) { \
        return new_##name##_with_cap(DEFAULT_SET_CAP); \
    } \
    static inline void free_##name(struct name* set) { \
        free_htable(&set->htable); \
    } \
    static inline bool insert_in_##name(struct name* set, T key) { \
        void* values = NULL; \
        return insert_in_htable( \
            &set->htable, &values, \
            &key, sizeof(T), \
            NULL, 0, \
            hash(&key), compare); \
    } \
    static inline const T* find_in_##name(struct name* set, T key) { \
        return find_in_htable( \
            &set->htable, set->htable.keys, \
            &key, sizeof(T), sizeof(T), \
            hash(&key), compare); \
    } \
    static inline bool remove_from_##name(struct name* set, T key) { \
        return remove_from_htable( \
            &set->htable, NULL, \
            &key, sizeof(T), 0, \
            hash(&key), compare); \
    } \
    static inline void clear_##name(struct name* set) { \
        clear_htable(&set->htable); \
    }

#define SET(name, T) \
    DEFAULT_HASH(hash_##name##_elem, T) \
    DEFAULT_COMPARE(compare_##name##_elem, T) \
    CUSTOM_SET(name, T, hash_##name##_elem, compare_##name##_elem)

#endif
