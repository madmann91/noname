#include <assert.h>
#include <string.h>

#include "utils/format.h"
#include "utils/log.h"

enum msg_type {
    MSG_ERR,
    MSG_WARN,
    MSG_NOTE
};

static inline void log_msg(struct log* log, enum msg_type type, const struct loc* loc, const char* fmt, union format_arg* args) {
    unsigned style = 0;
    const char* header = "";
    switch (type) {
        case MSG_ERR:  style = STYLE_ERROR;   header = "error";   log->errors++; break;
        case MSG_WARN: style = STYLE_WARNING; header = "warning"; log->warns++;  break;
        case MSG_NOTE: style = STYLE_NOTE;    header = "note";    break;
    }
    format(
        &log->out, "%0:$%1:s:%2:$ ",
        FORMAT_ARGS({ .style = style }, { .s = header }, { .style = 0 }));
    format(&log->out, fmt, args);
    format(&log->out, "\n", NULL);
    if (loc && loc->file) {
        format(
            &log->out,
            memcmp(&loc->begin, &loc->end, sizeof(loc->begin))
                ? "  in %0:$%1:s(%2:u, %3:u -- %4:u, %5:u)%6:$\n"
                : "  in %0:$%1:s(%2:u, %3:u)%6:$\n",
            FORMAT_ARGS(
                { .style = STYLE_LOC },
                { .s = loc->file ? loc->file : "<unknown>" },
                { .u = loc->begin.row },
                { .u = loc->begin.col },
                { .u = loc->end.row },
                { .u = loc->end.col },
                { .style = 0 }));
    }
}

void log_error(struct log* log, const struct loc* loc, const char* fmt, union format_arg* args) {
    log_msg(log, MSG_ERR, loc, fmt, args);
}

void log_warn(struct log* log, const struct loc* loc, const char* fmt, union format_arg* args) {
    log_msg(log, MSG_WARN, loc, fmt, args);
}

void log_note(struct log* log, const struct loc* loc, const char* fmt, union format_arg* args) {
    log_msg(log, MSG_NOTE, loc, fmt, args);
}
