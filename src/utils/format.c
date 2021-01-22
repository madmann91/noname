#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>

#include "utils/format.h"
#include "utils/utils.h"
#include "ir/print.h"

#define MAX_DIGITS 32

static inline void grow_buf(struct format_buf** buf, size_t size) {
    struct format_buf* cur = *buf;
    while (cur->next) {
        if (cur->next->cap >= size) {
            cur->next->size = 0;
            *buf = cur;
            return;
        }
        cur = cur->next;
    }
    size_t cap = cur->cap;
    if (size > cur->cap)
        cap = size;
    struct format_buf* next = xmalloc(sizeof(struct format_buf));
    next->data = xmalloc(cap);
    next->cap = cap;
    next->size = 0;
    next->next = NULL;
    cur->next = next;
    *buf = next;
}

static inline char* get_buf_data(struct format_buf** buf, size_t size) {
    if ((*buf)->cap - (*buf)->size < size)
        grow_buf(buf, size);
    return (*buf)->data + (*buf)->size;
}

static inline void write_to_buf(struct format_buf** buf, const char* begin, const char* end) {
    char* data = get_buf_data(buf, end - begin);
    memcpy(data, begin, end - begin);
    (*buf)->size += end - begin;
}

static inline void add_to_buf(struct format_buf** buf, const char* str) {
    write_to_buf(buf, str, str + strlen(str));
}

void reset_format_buf(struct format_buf* buf) {
    while (buf) {
        buf->size = 0;
        buf = buf->next;
    }
}

void dump_format_buf(struct format_buf* buf, FILE* fp) {
    while (buf) {
        fwrite(buf->data, 1, buf->size, fp);
        buf = buf->next;
    }
}

void free_format_buf(struct format_buf* buf) {
    while (buf) {
        struct format_buf* next = buf->next;
        free(buf->data);
        free(buf);
        buf = next;
    }
}

void format(struct format_out* out, const char* fmt, const union format_arg* args) {
    const char* ptr = fmt;
    while (true) {
        const char* prev = ptr;
        while (*ptr) {
            if (*ptr == '%') {
                if (ptr[1] != '%')
                    break;
                ptr++;
            }
            ptr++;
        }
        write_to_buf(&out->buf, prev, ptr);
        if (!*ptr)
            break;

        assert(*ptr == '%');
        ptr++;

        unsigned long index = strtoul(ptr, (char**)&ptr, 10);
        assert(*ptr == ':' && "missing colon in format argument");
        ptr++;

        char* data = get_buf_data(&out->buf, MAX_DIGITS);
        size_t n = 0;
        bool hex = false;
        if (*ptr == 'h') {
            hex = true;
            ptr++;
        }
        switch (*ptr) {
            case 'i': n = snprintf(data, MAX_DIGITS, "%"PRIiMAX, args[index].i); break;
            case 'p': n = snprintf(data, MAX_DIGITS, "%p", args[index].p);       break;
            case 'c': n = 1; data[0] = args[index].c;                            break;
            case 'u':
                n = snprintf(data, MAX_DIGITS, hex ? "0x%"PRIxMAX : "%"PRIuMAX, args[index].u);
                hex = false;
                break;
            case 'd':
                n = snprintf(data, MAX_DIGITS, hex ? "%a" : "%f", args[index].d);
                hex = false;
                break;
            case 's':
                write_to_buf(&out->buf, args[index].s, args[index].s + strlen(args[index].s));
                break;
            case 'e':
                print_node(out, args[index].n);
                break;
            case '$':
                if (out->color) {
                    add_to_buf(&out->buf, "\33[");
                    unsigned style = args[index].style;
                    if (style == 0)
                        add_to_buf(&out->buf, "m");
                    else {
                        if (style & STYLE_BOLD)
                            add_to_buf(&out->buf, "1;");
                        if (style & STYLE_ITALIC)
                            add_to_buf(&out->buf, "3;");
                        if (style & STYLE_UNDERLINE)
                            add_to_buf(&out->buf, "4;");
                        if (style & COLOR_WHITE)
                            add_to_buf(&out->buf, "37;");
                        else if (style & COLOR_BLACK)
                            add_to_buf(&out->buf, "30;");
                        else if (style & COLOR_RED)
                            add_to_buf(&out->buf, "31;");
                        else if (style & COLOR_GREEN)
                            add_to_buf(&out->buf, "32;");
                        else if (style & COLOR_BLUE)
                            add_to_buf(&out->buf, "34;");
                        else if (style & COLOR_CYAN)
                            add_to_buf(&out->buf, "36;");
                        else if (style & COLOR_MAGENTA)
                            add_to_buf(&out->buf, "35;");
                        else if (style & COLOR_YELLOW)
                            add_to_buf(&out->buf, "33;");
                        out->buf->data[out->buf->size-1] = 'm';
                    }
                }
                break;
        }
        assert(!hex);
        out->buf->size += n;
        ptr++;
    }
}
