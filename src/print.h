#ifndef PRINT_H
#define PRINT_H

#include <stddef.h>
#include <stdbool.h>
#include "exp.h"
#include "format.h"

struct printer {
    struct fmtbuf* buf;
    const char* tab;
    size_t indent;
    bool color;
};

void print_exp(struct printer*, exp_t);
void dump_exp(exp_t);
void dump_vars(vars_t);

#endif
