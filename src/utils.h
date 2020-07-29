#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdlib.h>

#define BUF_SIZE 16
#define ALLOC_BUF(name, type, size) \
    type name##_buf[size <= BUF_SIZE ? size : 1]; \
    type* name = size <= BUF_SIZE ? name##_buf : xmalloc(sizeof(type) * size);
#define FREE_BUF(name) \
    if (name != name##_buf) free(name);

void* xmalloc(size_t);
void* xrealloc(void*, size_t);
void* xcalloc(size_t, size_t);

#endif
