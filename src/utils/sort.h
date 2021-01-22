#ifndef UTILS_SORT_H
#define UTILS_SORT_H

#include <stddef.h>

// This is a simple shell sort implementation
#define CUSTOM_SORT(name, T, is_less_than) \
    static inline void name(T* p, size_t n) { \
        static const size_t gaps[] = { 701, 301, 132, 57, 23, 10, 4, 1 }; \
        static const size_t gap_count = sizeof(gaps) / sizeof(gaps[0]); \
        for (size_t k = 0; k < gap_count; ++k) { \
            size_t gap = gaps[k]; \
            for (size_t i = gap; i < n; ++i) { \
                T e = p[i]; \
                size_t j = i; \
                for (; j >= gap && is_less_than(&e, &p[j - gap]); j -= gap) \
                    p[j] = p[j - gap]; \
                p[j] = e; \
            } \
        } \
    }

#define SORT(name, T) \
    static inline bool is_less_than_for_##name(const T* left, const T* right) { \
        return (*left) < (*right); \
    } \
    CUSTOM_SORT(name, T, is_less_than_for_##name)

#endif
