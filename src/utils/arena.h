#ifndef UTILS_ARENA_H
#define UTILS_ARENA_H

#include <stddef.h>

typedef struct arena* arena_t;

arena_t new_arena(void);
void free_arena(arena_t);
void reset_arena(arena_t*);
void* alloc_from_arena(arena_t*, size_t);

#endif
