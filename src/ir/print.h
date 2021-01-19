#ifndef IR_PRINT_H
#define IR_PRINT_H

#include <stddef.h>
#include <stdbool.h>

#include "utils/format.h"
#include "ir/exp.h"

void print_exp(struct format_out*, exp_t);
void dump_exp(exp_t);
void dump_vars(vars_t);

#endif
