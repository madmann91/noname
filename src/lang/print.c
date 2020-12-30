#include "lang/ast.h"
#include "utils/format.h"
#include "ir/print.h"

void print_simple_exp(struct printer* printer, exp_t exp) {
    switch (exp->tag) {
        default:
            // Default to IR syntax
            print_exp(printer, exp);
            break;
    }
}
