#ifndef UTILS_LOG_H
#define UTILS_LOG_H

#include <stdbool.h>
#include <stddef.h>

#include "utils/format.h"

struct loc {
    const char* file;
    struct pos {
        int row, col;
        const char* ptr;
    } begin, end;
};

struct log {
    struct printer printer;
    size_t errors, warns;
};

void log_error(struct log*, const struct loc*, const char*, union fmtarg*);
void log_warn(struct log*, const struct loc*, const char*, union fmtarg*);
void log_note(struct log*, const struct loc*, const char*, union fmtarg*);

#endif
