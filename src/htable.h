#ifndef HTABLE_H
#define HTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*cmpfn_t)(const void*, const void*);

struct htable {
    void* elems;
    uint32_t* hashes;
    size_t elem_cap;
    size_t elem_count;
    size_t elem_size;
    cmpfn_t cmp;
};

struct htable new_htable(size_t, size_t, cmpfn_t);
void free_htable(struct htable*);
bool insert_in_htable(struct htable*, void*, uint32_t, void**);
void* find_in_htable(struct htable*, const void*, uint32_t);

#endif
