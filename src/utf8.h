#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MIN_UTF8_BYTES 2
#define MAX_UTF8_BYTES 4

// Returns true if the character is the beginning of a UTF-8 multi-byte character.
static inline bool is_utf8_multibyte(char c) {
    return ((uint8_t)c) & 0x80;
}

// Returns the number of bytes in the next UTF-8 character.
static inline size_t count_utf8_bytes(const char* str) {
    uint8_t c = *(uint8_t*)str;
    size_t n = 0;
    while (c & 0x80 && n <= MAX_UTF8_BYTES) c <<= 1, n++;
    return n;
}

// Check that the next UTF-8 character is well-formed.
static inline bool check_utf8_bytes(const char* str, size_t n) {
    if (n > MAX_UTF8_BYTES || n < MIN_UTF8_BYTES)
        return false;
    for (size_t i = 1; i < n; ++i) {
        uint8_t c = ((uint8_t*)str)[i];
        if ((c & 0xC0) != 0x80)
            return false;
    }
    return true;
}

// Eats a UTF-8 character and returns the number of bytes eaten.
static inline size_t eat_utf8_bytes(const char* str) {
    size_t n = count_utf8_bytes(str);
    return check_utf8_bytes(str, n) ? n : 1;
}

#endif
