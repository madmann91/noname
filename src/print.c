#include <assert.h>
#include "utils.h"
#include "print.h"

static inline void print(struct printer* printer, const char* fmt, const union fmtarg* args) {
    format(printer->color, &printer->buf, fmt, args);
}

static inline void print_keyword(struct printer* printer, const char* keyword) {
    print(
        printer, "%0:$%1:s%2:$",
        FMT_ARGS({ .style = STYLE_KEYWORD }, { .s = keyword }, { .style = 0 }));
}

static inline void print_newline(struct printer* printer) {
    print(printer, "\n", NULL);
    for (size_t i = 0, n = printer->indent; i < n; ++i)
        print(printer, "%0:s", FMT_ARGS({ .s = printer->tab }));
}

static inline void print_lit(struct printer* printer, exp_t type, const union lit* lit) {
    assert(type->tag == EXP_REAL || type->tag == EXP_INT || type->tag == EXP_NAT);
    print(printer, "(", NULL);
    print_keyword(printer, "lit");
    print(printer, " ", NULL);
    print_exp(printer, type);
    print(printer, " ", NULL);
    print(
        printer, type->tag == EXP_REAL ? "%0:hd" : "%1:hu",
        FMT_ARGS({ .d = lit->real_val }, { .u = lit->int_val }));
    print(printer, ")", NULL);
}

static inline void print_var(struct printer* printer, exp_t var) {
    print(printer, "(#%0:u : ", FMT_ARGS({ .u = var->var.index }));
    print_exp(printer, var->type);
    print(printer, ")", NULL);
}

void print_exp(struct printer* printer, exp_t exp) {
    assert(exp->type || exp->tag == EXP_UNI);
    switch (exp->tag) {
        case EXP_VAR:
            print(printer, "#%0:u", FMT_ARGS({ .u = exp->var.index }));
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
            print(printer, "(", NULL);
            print_keyword(printer,
                exp->tag == EXP_TOP ? "top" :
                exp->tag == EXP_BOT ? "bot" :
                "wild");
            print(printer, " ", NULL);
            print_exp(printer, exp->type);
            print(printer, ")", NULL);
            break;
        case EXP_INT:
        case EXP_REAL:
            print(printer, "(", NULL);
            print_keyword(printer, exp->tag == EXP_REAL ? "real" : "int");
            print(printer, " ", NULL);
            print_exp(printer, exp->real.bitwidth);
            print(printer, ")", NULL);
            break;
        case EXP_LIT:
            print_lit(printer, exp->type, &exp->lit);
            break;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            print(printer, "(", NULL);
            print_keyword(
                printer,
                exp->tag == EXP_SUM  ? "sum" :
                exp->tag == EXP_PROD ? "prod" : "tup");
            print(printer, " ", NULL);
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                print_exp(printer, exp->tup.args[i]);
                if (i != n - 1)
                    print(printer, " ", NULL);
            }
            print(printer, ")", NULL);
            break;
        case EXP_INJ:
            print(printer, "(", NULL);
            print_keyword(printer, "inj");
            print(printer, " ", NULL);
            print_exp(printer, exp->type);
            print(printer, " $0:u ", FMT_ARGS({ .u = exp->inj.index }));
            print_exp(printer, exp->inj.arg);
            print(printer, ")", NULL);
            break;
        case EXP_PI:
            print(printer, "(", NULL);
            print_keyword(printer, "pi");
            print(printer, " ", NULL);
            print_exp(printer, exp->pi.dom);
            print(printer, " ", NULL);
            print_exp(printer, exp->pi.codom);
            print(printer, ")", NULL);
            break;
        case EXP_ABS:
            print(printer, "(", NULL);
            print_keyword(printer, "abs");
            print(printer, " ", NULL);
            print_exp(printer, exp->type);
            print(printer, " ", NULL);
            print_exp(printer, exp->abs.body);
            print(printer, ")", NULL);
            break;
        case EXP_APP:
            print(printer, "(", NULL);
            print_exp(printer, exp->app.left);
            print(printer, " ", NULL);
            print_exp(printer, exp->app.right);
            print(printer, ")", NULL);
            break;
        case EXP_LET:
        case EXP_LETREC: {
            bool rec = exp->tag == EXP_LETREC;
            print(printer, "(", NULL);
            print_keyword(printer, rec ? "letrec" : "let");
            printer->indent++;
            print_newline(printer);
            print(printer, "(", NULL);
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i) {
                print_var(printer, exp->let.vars[i]);
                if (i != n - 1) {
                    print_newline(printer);
                    print(printer, " ", NULL);
                }
            }
            print(printer, ")", NULL);
            print_newline(printer);
            print(printer, "(", NULL);
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i) {
                print_exp(printer, exp->let.vals[i]);
                if (i != n - 1) {
                    print_newline(printer);
                    print(printer, " ", NULL);
                }
            }
            print(printer, ")", NULL);
            print_newline(printer);
            print_exp(printer, exp->let.body);
            print(printer, ")", NULL);
            printer->indent--;
            break;
        }
        case EXP_MATCH:
            print(printer, "(", NULL);
            print_keyword(printer, "match");
            printer->indent++;
            if (exp->match.pat_count > 0)
                print_newline(printer);
            else
                print(printer, " ", NULL);
            print(printer, "(", NULL);
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                print(printer, "(", NULL);
                print_keyword(printer, "case");
                print(printer, " ", NULL);
                print_exp(printer, exp->match.pats[i]);
                print(printer, " ", NULL);
                print_exp(printer, exp->match.vals[i]);
                print(printer, ")", NULL);
                if (i != n - 1) {
                    print_newline(printer);
                    print(printer, " ", NULL);
                }
            }
            print(printer, ")", NULL);
            print_newline(printer);
            print_exp(printer, exp->match.arg);
            print(printer, ")", NULL);
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
    printf("\n");
}

void dump_fvs(fvs_t fvs) {
    printf("{");
    if (fvs->count > 0) {
        char data[PRINT_BUF_SIZE];
        struct fmtbuf buf = { .data = data, .cap = sizeof(data) };
        struct printer printer = {
            .buf    = &buf,
            .tab    = "  ",
            .color  = is_color_supported(stdout),
            .indent = 0
        };
        print(&printer, " ", NULL);
        for (size_t i = 0, n = fvs->count; i < n; ++i) {
            print_exp(&printer, fvs->vars[i]);
            if (i != n - 1)
                print(&printer, ", ", NULL);
        }
        print(&printer, " ", NULL);
        dump_fmtbuf(&buf, stdout);
        free_fmtbuf(buf.next);
    }
    printf("}\n");
}
