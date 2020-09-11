#include <assert.h>
#include "log.h"
#include "vec.h"

struct file_data {
    const char* name;
    const char* begin;
    size_t size;
};

struct log {
    struct fmtbuf* fmtbuf;
    struct file_data* files;
    bool color;
};

log_t new_log(struct fmtbuf* fmtbuf, bool color) {
    log_t log = xmalloc(sizeof(struct log));
    log->fmtbuf = fmtbuf;
    log->color  = color;
    log->files  = NEW_VEC(struct file_data);
    return log;
}

void free_log(log_t log) {
    FREE_VEC(log->files);
    free(log);
}

static struct file_data* find_file(log_t log, const char* name) {
    for (size_t i = 0, n = VEC_SIZE(log->files); i < n; i++) {
        if (!strcmp(log->files[i].name, name))
            return &log->files[i];
    }
    return NULL;
}

void add_source_file_to_log(log_t log, const char* name, const char* begin, size_t size) {
    assert(find_file(log, name) == NULL);
    VEC_PUSH(log->files, (struct file_data) { name, begin, size });
}

void log_msg(log_t log, enum msg_type type, const struct loc* loc, const char* fmt, union fmtarg* args) {
    unsigned style = 0;
    const char* header = "";
    switch (type) {
        case MSG_ERR:  style = STYLE_ERROR;   header = "error";   break;
        case MSG_WARN: style = STYLE_WARNING; header = "warning"; break;
        case MSG_NOTE: style = STYLE_NOTE;    header = "note";    break;
    }
    format(
        &log->fmtbuf,
        log->color ? "%0:$%1:s:%2:$ " : "%1:s: ",
        FMT_ARGS({ .style = style }, { .s = header }, { .style = 0 }));
    format(&log->fmtbuf, fmt, args);
    format(&log->fmtbuf, "\n", NULL);
    if (loc) {
        format(
            &log->fmtbuf,
            log->color ? "  in %0:$" : "  in ",
            FMT_ARGS({ .style = STYLE_LOC }));
        format(
            &log->fmtbuf,
            memcmp(&loc->begin, &loc->end, sizeof(loc->begin))
                ? "%0:s(%1:u, %2:u -- %3:u, %4:u)"
                : "%0:s(%1:u, %2:u)",
            FMT_ARGS(
                { .s = loc->file ? loc->file : "<unknown>" },
                { .u = loc->begin.row },
                { .u = loc->begin.col },
                { .u = loc->end.row },
                { .u = loc->end.col }));
        format(&log->fmtbuf, log->color ? "%0:$\n" : "\n", FMT_ARGS({ .style = 0 }));
    }
}
