#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include "format.h"

struct loc {
    const char* file;
    struct {
        int row, col;
        const char* ptr;
    } begin, end;
};

enum msg_type {
    MSG_ERR,
    MSG_WARN,
    MSG_NOTE
};

typedef struct log* log_t;

log_t new_log(struct fmtbuf*, bool);
void free_log(log_t);
void add_source_file_to_log(log_t, const char*, const char*, size_t);
void log_msg(log_t, enum msg_type, const struct loc*, const char*, union fmtarg*);

#endif
