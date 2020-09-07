#ifndef VEC_H
#define VEC_H

#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <stdalign.h>
#include "utils.h"

struct vec {
    size_t cap;
    size_t size;
    char ptr[];
};

// Helper macros
#define NEW_VEC(type) \
    ((type*)(new_vec(sizeof(type) * DEFAULT_CAP)->ptr))
#define FREE_VEC(ptr) \
    do { free_vec(get_vec_from_ptr(ptr)); } while (false)
#define PUSH_TO_VEC(ptr, ...) \
    do { push_to_vec(get_vec_from_ptr(ptr), &(__VA_ARGS__), sizeof(*(ptr))); } while (false)
#define POP_FROM_VEC(ptr) \
    do { pop_from_vec(get_vec_from_ptr(ptr), sizeof(*(ptr))); } while (false)
#define VEC_SIZE(ptr) \
    (get_vec_from_ptr(ptr)->size / sizeof(*(ptr)))
#define RESIZE_VEC(ptr, size) \
    do { resize_vec(get_vec_from_ptr(ptr), (size) * sizeof(*(ptr))); } while (false)
#define CLEAR_VEC(ptr) RESIZE_VEC(ptr, 0)
#define SHRINK_VEC(ptr, size) \
    do { shrink_vec(get_vec_from_ptr(ptr), sizeof(*(ptr)) * (size)); } while (false)

static inline struct vec* get_vec_from_ptr(void* ptr) {
    return (struct vec*)((char*)ptr - offsetof(struct vec, ptr));
}

static inline struct vec* new_vec(size_t cap) {
    struct vec* vec = xmalloc(sizeof(struct vec) + cap);
    vec->cap = cap;
    vec->size = 0;
    return vec;
}

static inline void free_vec(struct vec* vec) {
    free(vec);
}

static inline void resize_vec(struct vec* vec, size_t size) {
    if (vec->cap < size) {
        vec->cap = vec->size;
        vec = xrealloc(vec, sizeof(vec) + vec->cap);
    }
    vec->size = size;
}

static inline void shrink_vec(struct vec* vec) {
    vec = xrealloc(vec, sizeof(vec) + vec->size);
    vec->cap = vec->size;
}

static inline void push_to_vec(struct vec* vec, const void* elem, size_t elem_size) {
    if (vec->size + elem_size > vec->cap) {
        // Grow vector by 1.5x
        vec->cap = vec->cap + vec->cap / 2 + elem_size;
        vec = xrealloc(vec, sizeof(vec) + vec->cap);
    }
    memcpy(vec->ptr + vec->size, elem, elem_size);
    vec->size += elem_size;
}

static inline void pop_from_vec(struct vec* vec, size_t elem_size) {
    vec->size -= elem_size;
}

#endif
