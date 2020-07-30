#include <stdio.h>
#include "utils.h"

static inline void die(const char* msg) {
    fprintf(stderr, "%s\n", msg);
    abort();
}

void* xmalloc(size_t size) {
    if (size == 0)
        return NULL;
    void* ptr = malloc(size);
    if (!ptr)
        die("not enough memory");
    return ptr;
}

void* xrealloc(void* ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    ptr = realloc(ptr, size);
    if (!ptr)
        die("not enough memory");
    return ptr;
}

void* xcalloc(size_t len, size_t size) {
    if (size == 0 || len == 0)
        return NULL;
    void* ptr = calloc(len, size);
    if (!ptr)
        die("not enough memory");
    return ptr;
}

void memswp(void* p1, void* p2, size_t size) {
    char* s1 = p1, * s2 = p2;
    for (size_t i = 0; i < size; ++i) {
        char c = s1[i];
        s1[i] = s2[i];
        s2[i] = c;
    }
}