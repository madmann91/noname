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

typedef struct ir_parser* ir_parser_t;

// Creates a parser object that places expressions in the given module.
// Errors are reported in the log.
ir_parser_t new_ir_parser(
    mod_t mod,
    struct log* log,
    const char* file_name,
    const char* data,
    size_t data_size);

void free_ir_parser(ir_parser_t);
exp_t parse_exp(ir_parser_t);

#endif
