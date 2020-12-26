#ifndef UTILS_FORMAT_H
#define UTILS_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define STYLE_KEYWORD (STYLE_BOLD | COLOR_GREEN)
#define STYLE_ERROR   (STYLE_BOLD | COLOR_RED)
#define STYLE_WARNING (STYLE_BOLD | COLOR_YELLOW)
#define STYLE_NOTE    (STYLE_BOLD | COLOR_CYAN)
#define STYLE_LOC     (STYLE_BOLD | COLOR_WHITE)

/*
 * This small formatting API can format to a buffer or a file.
 * The syntax for the formatting string differs from that of `printf`.
 * Arguments are introduced with `%i:t` where `i` is an index into the
 * array of arguments and `t` is the type of the argument.
 * Available types are:
 *
 *   - `i`: Signed integer
 *   - `u`: Unsigned integer
 *   - `d`: Double
 *   - `hu`: Unsigned integer, hexadecimal format
 *   - `hd`: Double, hexadecimal format
 *   - `c`: Character
 *   - `s`: String
 *   - `p`: Pointer
 *   - `e`: Expression
 *   - `$`: Style
 */

struct fmtbuf {
    struct fmtbuf* next;
    size_t cap;
    size_t size;
    char* data;
};

struct exp;

union fmtarg {
    uintmax_t   u;
    intmax_t    i;
    double      d;
    char        c;
    const char* s;
    const void* p;
    const struct exp* e;
    enum {
        STYLE_BOLD      = 0x01,
        STYLE_ITALIC    = 0x02,
        STYLE_UNDERLINE = 0x04,
        COLOR_WHITE     = 0x08,
        COLOR_BLACK     = 0x10,
        COLOR_RED       = 0x20,
        COLOR_GREEN     = 0x40,
        COLOR_BLUE      = 0x80,
        COLOR_CYAN      = 0x100,
        COLOR_MAGENTA   = 0x200,
        COLOR_YELLOW    = 0x400,
    } style;
};

struct printer {
    struct fmtbuf* buf;
    void (*print_exp)(struct printer*, const struct exp*);
    const char* tab;
    size_t indent;
    bool color;
};

#define FMT_ARGS(...) (union fmtarg[]) { __VA_ARGS__ }

void reset_fmtbuf(struct fmtbuf*);
void dump_fmtbuf(struct fmtbuf*, FILE*);
void free_fmtbuf(struct fmtbuf*);

void print(struct printer*, const char*, const union fmtarg*);

#endif
