#include "assert.h"
#include "print.h"

void print_exp(struct printer* printer, exp_t exp) {
    switch (exp->tag) {
        case EXP_BVAR:
            format(&printer->buf, "#%0:u", FMT_ARGS({ .u = exp->bvar.index }));
            break;
        case EXP_FVAR:
            format(&printer->buf, "%0:s", FMT_ARGS({ .s = exp->fvar.name }));
            break;
        case EXP_STAR:
            format(&printer->buf, "*", NULL);
            break;
        case EXP_ABS:
            format(&printer->buf, "\\ _ : ", NULL);
            assert(exp->type->tag == EXP_PI);
            print_exp(printer, exp->type->pi.dom); 
            format(&printer->buf, " . ", NULL);
            print_exp(printer, exp->abs.body);
            break;
        default:
            break;
    }
}
