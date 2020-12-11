#ifndef IR_PARSE_H
#define IR_PARSE_H

#include <stdio.h>

#include "utils/log.h"
#include "ir/exp.h"

/*
 * The expression parser can understand the syntax emitted
 * by `print_exp()`. However, certain guarantees must be met
 * (in particular with respect to scoping), so that the expressions
 * can be reconstructed. This means that incorrect expressions
 * cannot be printed and parsed again.
 */

exp_t parse_exp(
    mod_t mod,
    struct log* log,
    const char* file_name,
    const char* data,
    size_t data_size);

#endif
