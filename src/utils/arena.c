#include <stdlib.h>
#include <stdalign.h>

#include "utils/arena.h"
#include "utils/utils.h"

#define INITIAL_ARENA_SIZE 4096

struct arena {
    arena_t next, prev;
    size_t size;
    size_t cap;
    alignas(max_align_t) char data[];
};

static arena_t alloc_block(arena_t prev, size_t cap) {
    arena_t arena = xmalloc(sizeof(struct arena) + cap);
    arena->prev = prev;
    arena->next = NULL;
    arena->cap = cap;
    arena->size = 0;
    return arena;
}

arena_t new_arena() {
    return alloc_block(NULL, INITIAL_ARENA_SIZE);
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
    if (size == 0)
        return NULL;

    // Align the size to the largest alignment requirement
    size_t pad = size % sizeof(max_align_t);
    size = pad != 0 ? size + sizeof(max_align_t) - pad : size;

    // Find a block where the allocation can be made
    arena_t cur = *arena;
    while (remaining_size(cur) < size) {
        if (cur->next)
            cur = cur->next;
        else {
            arena_t next = alloc_block(cur, size > cur->cap ? round_to_pow2(size) : cur->cap);
            cur->next = next;
            cur = next;
            break;
        }
    }

    void* ptr = cur->data + cur->size;
    cur->size += size;
    *arena = cur;
    return ptr;
}
