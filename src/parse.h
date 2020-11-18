#ifndef PARSE_H
#define PARSE_H

#include <stdio.h>
#include "exp.h"
#include "log.h"

/*
 * The expression parser can understand the syntax emitted
 * by `print_exp()`. However, certain guarantees must be met
 * (in particular with respect to scoping), so that the expressions
 * can be reconstructed. This means that incorrect expressions
 * cannot be printed and parsed again.
 */

typedef struct parser* parser_t;

// Creates a parser object that places expressions in the given module.
// Errors are reported in the log.
parser_t new_parser(
    mod_t mod,
    struct log* log,
    const char* file_name,
    const char* data,
    size_t data_size);

void free_parser(parser_t);
exp_t parse_exp(parser_t);

#endif
