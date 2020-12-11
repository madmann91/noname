#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define IGNORE_ARGS(...)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

void* xmalloc(size_t);
void* xrealloc(void*, size_t);
void* xcalloc(size_t, size_t);
void memswp(void*, void*, size_t);

static inline void* deref_or_null(void** ptr) {
    return ptr ? *ptr : NULL;
}

static inline size_t round_to_pow2(size_t size) {
    size_t p = 1;
    while (p < size) p <<= 1;
    return p;
}

bool is_color_supported(FILE*);

#endif
