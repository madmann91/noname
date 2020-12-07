#include <stdlib.h>

#include "utils/arena.h"
#include "utils/utils.h"

struct arena {
    arena_t next, prev;
    size_t size;
    size_t cap;
    max_align_t data[];
};

static arena_t alloc_block(arena_t prev, size_t cap) {
    arena_t arena = xmalloc(sizeof(struct arena) + cap);
    arena->prev = prev;
    arena->next = NULL;
    arena->cap = cap;
    arena->size = 0;
    return arena;
}

arena_t new_arena(size_t cap) {
    return alloc_block(NULL, cap);
}

void free_arena(arena_t arena) {
    arena_t cur = arena->next;
    while (cur) {
        arena_t next = cur->next;
        free(cur);
        cur = next;
    }
    cur = arena->prev;
    while (cur) {
        arena_t prev = cur->prev;
        free(cur);
        cur = prev;
    }
    free(arena);
}

void reset_arena(arena_t* arena) {
    arena_t cur = *arena;
    if (!cur)
        return;
    while (cur->prev) {
        cur->size = 0;
        cur = cur->prev;
    }
    *arena = cur;
}

static inline size_t remaining_size(const arena_t arena) {
    return arena->cap - arena->size;
}

void* alloc_from_arena(arena_t* arena, size_t size) {
    if (size == 0) return NULL;
    arena_t cur = *arena;
    while (remaining_size(cur) < size) {
        if (cur->next)
            cur = cur->next;
        else {
            size_t cap = cur->cap < size ? round_to_pow2(size) : cur->cap;
            cur->next = alloc_block(cur, cap);
            cur = cur->next;
            break;
        }
    }
    void* ptr = (char*)cur->data + cur->size;
    cur->size += size;
    return ptr;
}
