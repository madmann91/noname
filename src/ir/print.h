#ifndef IR_PRINT_H
#define IR_PRINT_H

#include <stddef.h>
#include <stdbool.h>

#include "utils/format.h"
#include "ir/node.h"

void print_node(struct format_out*, node_t);
void dump_node(node_t);
void dump_vars(vars_t);

#endif
