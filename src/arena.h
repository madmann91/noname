#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct arena* arena_t;

arena_t new_arena(size_t);
void free_arena(arena_t);
void reset_arena(arena_t*);
void* alloc_from_arena(arena_t*, size_t);

#endif
