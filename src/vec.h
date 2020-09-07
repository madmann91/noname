#ifndef VEC_H
#define VEC_H

#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <stdalign.h>
#include "utils.h"

struct vec {
    bool on_stack : 1;
    size_t cap : sizeof(size_t) * CHAR_BIT - 1;
    size_t size;
    char ptr[];
};

// Helper macros
#define NEW_VEC(type) \
    ((type*)(new_vec(sizeof(type) * DEFAULT_CAP)->ptr))
#define NEW_STACK_VEC(name, type) \
    alignas(max_align_t) char name##_buf[sizeof(struct vec) + sizeof(type) * STACK_BUF_SIZE]; \
    struct vec* name##_vec = (struct vec*)name##_buf; \
    name##_vec->cap = STACK_BUF_SIZE; \
    name##_vec->size = 0; \
    name##_vec->on_stack = true; \
    type* name = (type*)name##_vec->ptr;
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
    vec->on_stack = false;
    return vec;
}

static inline void free_vec(struct vec* vec) {
    if (!vec->on_stack)
        free(vec);
}

static inline void resize_vec(struct vec* vec, size_t size) {
    if (vec->cap < size) {
        vec->cap = vec->cap * 2;
        if (vec->size > vec->cap)
            vec->cap = vec->size;
        vec = xrealloc(vec->on_stack ? NULL : vec, sizeof(vec) + vec->cap);
    }
    vec->size = size;
}

static inline void shrink_vec(struct vec* vec) {
    if (vec->on_stack)
        return;
    vec = xrealloc(vec, sizeof(vec) + vec->size);
    vec->cap = vec->size;
}

static inline void push_to_vec(struct vec* vec, const void* elem, size_t elem_size) {
    if (vec->size + elem_size > vec->cap) {
        size_t cap = (vec->cap + elem_size) * 2;
        vec = xrealloc(vec->on_stack ? NULL : vec, sizeof(vec) + cap);
    }
    memcpy(vec->ptr + vec->size, elem, elem_size);
    vec->size += elem_size;
}

static inline void pop_from_vec(struct vec* vec, size_t elem_size) {
    vec->size -= elem_size;
}

#endif
