#ifndef UTILS_LEXER_H
#define UTILS_LEXER_H

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "utils/log.h"
#include "utils/utf8.h"

struct lexer {
    const char* end;
    const char* file;
    struct log* log;
    struct pos  pos;
};

static inline void eat_char(struct lexer* lexer) {
    assert(lexer->pos.ptr != lexer->end);
    if (is_utf8_multibyte(*lexer->pos.ptr)) {
        size_t n = eat_utf8_bytes(lexer->pos.ptr);
        if (lexer->pos.ptr + n > lexer->end)
            n = lexer->end - lexer->pos.ptr;
        lexer->pos.ptr += n;
        lexer->pos.col++;
    } else {
        if (*lexer->pos.ptr == '\n')
            ++lexer->pos.row, lexer->pos.col = 1;
        else
            lexer->pos.col++;
        lexer->pos.ptr++;
    }
}

static inline void eat_spaces(struct lexer* lexer) {
    while (lexer->pos.ptr != lexer->end && isspace(*lexer->pos.ptr))
        eat_char(lexer);
}

static inline bool accept_char(struct lexer* lexer, char c) {
    if (*lexer->pos.ptr == c) {
        eat_char(lexer);
        return true;
    }
    return false;
}

static inline bool accept_str(struct lexer* lexer, const char* str) {
    size_t len = strlen(str);
    if (lexer->pos.ptr + len > lexer->end)
        return false;
    for (size_t i = 0; i < len; ++i) {
        if (str[i] != lexer->pos.ptr[i])
            return false;
    }
    const char* begin = lexer->pos.ptr;
    while (lexer->pos.ptr < begin + len)
        eat_char(lexer);
    return true;
}

#endif
