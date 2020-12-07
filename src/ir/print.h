#ifndef IR_PRINT_H
#define IR_PRINT_H

#include <stddef.h>
#include <stdbool.h>

#include "utils/format.h"
#include "exp.h"

struct ir_printer {
    struct fmtbuf* buf;
    const char* tab;
    size_t indent;
    bool color;
};

void print_exp(struct ir_printer*, exp_t);
void dump_exp(exp_t);
void dump_vars(vars_t);

#endif
