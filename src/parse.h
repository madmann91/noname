#ifndef PARSE_H
#define PARSE_H

#include <stdio.h>
#include "exp.h"
#include "log.h"

typedef struct parser* parser_t;

parser_t new_parser(mod_t, log_t, const char*, const char*, size_t);
void free_parser(parser_t);
exp_t parse_exp(parser_t);

#endif
