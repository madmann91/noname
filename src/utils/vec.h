#ifndef UTILS_VEC_H
#define UTILS_VEC_H

#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "utils/utils.h"

#define DEFAULT_VEC_CAP 8

#define VEC(name, T) \
    struct name { \
        size_t cap; \
        size_t size; \
        T* elems; \
    }; \
    static inline struct name new_##name##_with_cap(size_t cap) { \
        assert((cap & 1) == 0 && cap > 1); \
        return (struct name) { \
            .cap   = cap, \
            .size  = 0, \
            .elems = xmalloc(sizeof(T) * cap), \
        }; \
    } \
    static inline struct name new_##name##_on_stack(size_t cap, T* buf) { \
        assert((cap & 1) == 0 && cap > 1); \
        return (struct name) { \
            .cap   = cap | 1, \
            .size  = 0, \
            .elems = buf, \
        }; \
    } \
    static inline struct name new_##name(void) { \
        return new_##name##_with_cap(DEFAULT_VEC_CAP); \
    } \
    static inline void free_##name(struct name* vec) { \
        if ((vec->cap & 1) == 0) \
            free(vec->elems); \
        vec->elems = NULL; \
    } \
    static inline void grow_##name(struct name* vec, size_t cap) { \
        assert((cap & 1) == 0 && cap >= vec->cap); \
        T* elems; \
        if ((vec->cap & 1) == 0) { \
            elems = xrealloc(vec->elems, sizeof(T) * cap); \
        } else { \
            elems = xmalloc(sizeof(T) * cap); \
            memcpy(elems, vec->elems, sizeof(T) * vec->size); \
        } \
        vec->elems = elems; \
        vec->cap = cap; \
    } \
    static inline void push_to_##name(struct name* vec, T value) { \
        if (vec->size >= (vec->cap & ~((size_t)1))) \
            grow_##name(vec, vec->cap * 2); \
        vec->elems[vec->size++] = value; \
    } \
    static inline T pop_from_##name(struct name* vec) { \
        return vec->elems[--vec->size]; \
    }

#endif
