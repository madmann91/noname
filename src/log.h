#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include <stddef.h>

struct fmtbuf;
union fmtarg;

struct loc {
    const char* file;
    struct {
        int row, col;
        const char* ptr;
    } begin, end;
};

struct log {
    struct fmtbuf* buf;
    size_t errors, warns;
    bool color;
};

void log_error(struct log*, const struct loc*, const char*, union fmtarg*);
void log_warn(struct log*, const struct loc*, const char*, union fmtarg*);
void log_note(struct log*, const struct loc*, const char*, union fmtarg*);

#endif
