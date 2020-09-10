#include <assert.h>
#include "utils.h"
#include "print.h"

static inline void print_keyword(struct printer* printer, const char* keyword) {
    format(&printer->buf,
           printer->color ? "%0:$%1:s%2:$" : "$1:s",
           FMT_ARGS({ .style = STYLE_KEYWORD }, { .s = keyword }, { .style = 0 }));
}

static inline void print_newline(struct printer* printer) {
    format(&printer->buf, "\n", NULL);
    for (size_t i = 0, n = printer->indent; i < n; ++i)
        format(&printer->buf, "%0:s", FMT_ARGS({ .s = printer->tab }));
}

static inline void print_lit(struct printer* printer, exp_t type, const union lit* lit) {
    assert(type->tag == EXP_REAL || type->tag == EXP_INT || type->tag == EXP_NAT);
    format(&printer->buf, "(", NULL);
    print_keyword(printer, "lit");
    format(&printer->buf, " ", NULL);
    print_exp(printer, type);
    format(&printer->buf, " ", NULL);
    format(
        &printer->buf,
        type->tag == EXP_REAL ? "%0:hd" : "%1:hu",
        FMT_ARGS({ .d = lit->real_val }, { .u = lit->int_val }));
    format(&printer->buf, ")", NULL);
}

void print_exp(struct printer* printer, exp_t exp) {
    assert(exp->type || exp->tag == EXP_UNI);
    switch (exp->tag) {
        case EXP_BVAR:
            format(
                &printer->buf,
                "#%0:u.%1:u",
                FMT_ARGS({ .u = exp->bvar.index }, { .u = exp->bvar.sub_index }));
            break;
        case EXP_FVAR:
            format(&printer->buf, "(", NULL);
            print_keyword(printer, "fvar");
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->type);
            format(&printer->buf, " %0:u", FMT_ARGS({ .u = exp->fvar.index }));
            format(&printer->buf, ")", NULL);
            break;
        case EXP_UNI:
            print_keyword(printer, "uni");
            break;
        case EXP_STAR:
            print_keyword(printer, "star");
            break;
        case EXP_NAT:
            print_keyword(printer, "nat");
            break;
        case EXP_WILD:
        case EXP_BOT:
        case EXP_TOP:
            format(&printer->buf, "(", NULL);
            print_keyword(printer,
                exp->tag == EXP_TOP ? "top" :
                exp->tag == EXP_BOT ? "bot" :
                "wild");
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->type);
            format(&printer->buf, ")", NULL);
            break;
        case EXP_INT:
        case EXP_REAL:
            format(&printer->buf, "(", NULL);
            print_keyword(printer, exp->tag == EXP_REAL ? "real" : "int");
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->real.bitwidth);
            format(&printer->buf, ")", NULL);
            break;
        case EXP_LIT:
            print_lit(printer, exp->type, &exp->lit);
            break;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            format(&printer->buf, "(", NULL);
            print_keyword(
                printer,
                exp->tag == EXP_SUM  ? "sum" :
                exp->tag == EXP_PROD ? "prod" : "tup");
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->type);
            format(&printer->buf, " ", NULL);
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                print_exp(printer, exp->tup.args[i]);
                if (i != n - 1)
                    format(&printer->buf, " ", NULL);
            }
            format(&printer->buf, ")", NULL);
            break;
        case EXP_INJ:
            format(&printer->buf, "(", NULL);
            print_keyword(printer, "inj");
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->type);
            format(&printer->buf, " $0:u ", FMT_ARGS({ .u = exp->inj.index }));
            print_exp(printer, exp->inj.arg);
            format(&printer->buf, ")", NULL);
            break;
        case EXP_PI:
            format(&printer->buf, "(", NULL);
            print_keyword(printer, "pi");
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->pi.dom);
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->pi.codom);
            format(&printer->buf, ")", NULL);
            break;
        case EXP_ABS:
            format(&printer->buf, "(", NULL);
            print_keyword(printer, "abs");
            format(&printer->buf, " ", NULL);
            assert(exp->type->tag == EXP_PI);
            print_exp(printer, exp->type->pi.dom);
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->abs.body);
            format(&printer->buf, ")", NULL);
            break;
        case EXP_APP:
            format(&printer->buf, "(", NULL);
            print_keyword(printer, "app");
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->app.left);
            format(&printer->buf, " ", NULL);
            print_exp(printer, exp->app.right);
            format(&printer->buf, ")", NULL);
            break;
        case EXP_LET:
            format(&printer->buf, "(", NULL);
            print_keyword(printer, "let");
            printer->indent++;
            if (exp->let.bind_count > 1)
                print_newline(printer);
            else
                format(&printer->buf, " ", NULL);
            format(&printer->buf, "(", NULL);
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i) {
                print_exp(printer, exp->let.binds[i]);
                if (i != n - 1) {
                    print_newline(printer);
                    format(&printer->buf, " ", NULL);
                }
            }
            format(&printer->buf, ")", NULL);
            print_newline(printer);
            print_exp(printer, exp->let.body);
            format(&printer->buf, ")", NULL);
            printer->indent--;
            break;
        case EXP_MATCH:
            format(&printer->buf, "(", NULL);
            print_keyword(printer, "match");
            print_exp(printer, exp->match.arg);
            printer->indent++;
            print_newline(printer);
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                print_exp(printer, exp->match.pats[i]);
                format(&printer->buf, " ", NULL);
                print_exp(printer, exp->match.exps[i]);
                if (i != n - 1) {
                    format(&printer->buf, " ", NULL);
                    print_newline(printer);
                }
            }
            format(&printer->buf, ")", NULL);
            printer->indent--;
            break;
        default:
            assert(false && "invalid expression tag");
            break;
    }
}

void dump_exp(exp_t exp) {
    char data[PRINT_BUF_SIZE];
    struct fmtbuf buf = { .data = data, .cap = sizeof(data) };
    struct printer printer = {
        .buf    = &buf,
        .tab    = "  ",
        .color  = is_color_supported(stdout),
        .indent = 0
    };
    print_exp(&printer, exp);
    dump_fmtbuf(&buf, stdout);
    free_fmtbuf(buf.next);
    fprintf(stdout, "\n");
}
