#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef NDEBUG
#define STACK_BUF_SIZE 16
#define PRINT_BUF_SIZE 1024
#define DEFAULT_CAP 8
#define DEFAULT_ARENA_SIZE 4096
#else
// Force local buffers to be of size 1,
// so that most allocations are on the heap and
// can easily be traced with tools like valgrind.
#define STACK_BUF_SIZE 1
#define PRINT_BUF_SIZE 8
#define DEFAULT_CAP 1
#define DEFAULT_ARENA_SIZE 1024
#endif

#define NEW_BUF(name, type, size) \
    type name##_buf[STACK_BUF_SIZE]; \
    type* name = size <= STACK_BUF_SIZE ? name##_buf : xmalloc(sizeof(type) * size);
#define FREE_BUF(name) \
    if (name != name##_buf) free(name);

void* xmalloc(size_t);
void* xrealloc(void*, size_t);
void* xcalloc(size_t, size_t);
void memswp(void*, void*, size_t);

static inline size_t round_to_pow2(size_t size) {
    size_t p = 1;
    while (p < size) p <<= 1;
    return p;
}

bool is_color_supported(FILE*);

#endif
