#ifndef PRINT_H
#define PRINT_H

#include <stddef.h>
#include <stdbool.h>
#include "exp.h"
#include "format.h"

struct printer {
    struct fmtbuf* buf;
    size_t indent;
    bool   color;
};

void print_exp(struct printer*, exp_t);
void print_pat(struct printer*, pat_t);

#endif
