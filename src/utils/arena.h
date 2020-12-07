#ifndef UTILS_ARENA_H
#define UTILS_ARENA_H

#include <stddef.h>

#ifndef NDEBUG
#define DEFAULT_ARENA_SIZE 1
#else
#define DEFAULT_ARENA_SIZE 4096
#endif

typedef struct arena* arena_t;

arena_t new_arena(size_t);
void free_arena(arena_t);
void reset_arena(arena_t*);
void* alloc_from_arena(arena_t*, size_t);

#endif
