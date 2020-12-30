#include <assert.h>

#include "utils/utils.h"
#include "ir/print.h"

#define PRINT_BUF_SIZE 256

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

static inline void print_lit(struct printer* printer, exp_t type, const struct lit* lit) {
    print(printer, "(", NULL);
    print_keyword(printer, "lit");
    print(printer, " ", NULL);
    print_exp(printer, type);
    print(printer, " ", NULL);
    print(
        printer, lit->tag == LIT_FLOAT ? "%0:hd" : "%1:hu",
        FMT_ARGS({ .d = lit->float_val }, { .u = lit->int_val }));
    print(printer, ")", NULL);
}

static inline void print_var_decl(struct printer* printer, exp_t var) {
    if (is_unbound_var(var))
        print(printer, "(_ : ", NULL);
    else
        print(printer, "(#%0:u : ", FMT_ARGS({ .u = var->var.index }));
    print_exp(printer, var->type);
    print(printer, ")", NULL);
}

static void print_exp_or_pat(struct printer* printer, exp_t exp, bool is_pat) {
    assert(exp->type || exp->tag == EXP_UNI);
    switch (exp->tag) {
        case EXP_ERR:
            print(printer, "%0:$<error", FMT_ARGS({ .style = STYLE_ERROR }));
            if (exp->type != exp) {
                print(printer, " : %0:$", FMT_ARGS({ .style = 0 }));
                print_exp(printer, exp->type);
                print(printer, "%0:$", FMT_ARGS({ .style = STYLE_ERROR }));
            }
            print(printer, ">%0:$", FMT_ARGS({ .style = 0 }));
            break;
        case EXP_VAR:
            if (is_pat)
                print_var_decl(printer, exp);
            else
                print(printer, "#%0:u", FMT_ARGS({ .u = exp->var.index }));
            break;
        case EXP_UNI:   print_keyword(printer, "uni");   break;
        case EXP_STAR:  print_keyword(printer, "star");  break;
        case EXP_NAT:   print_keyword(printer, "nat");   break;
        case EXP_INT:   print_keyword(printer, "int");   break;
        case EXP_FLOAT: print_keyword(printer, "float"); break;
        case EXP_BOT:
        case EXP_TOP:
            print(printer, "(", NULL);
            print_keyword(printer, exp->tag == EXP_TOP ? "top" : "bot");
            print(printer, " ", NULL);
            print_exp(printer, exp->type);
            print(printer, ")", NULL);
            break;
        case EXP_LIT:
            print_lit(printer, exp->type, &exp->lit);
            break;
        case EXP_SUM:
        case EXP_PROD:
            is_pat = false;
            // fallthrough
        case EXP_TUP:
            print(printer, "(", NULL);
            print_keyword(
                printer,
                exp->tag == EXP_SUM  ? "sum" :
                exp->tag == EXP_PROD ? "prod" : "tup");
            print(printer, " (", NULL);
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                print_exp_or_pat(printer, exp->tup.args[i], is_pat);
                if (i != n - 1)
                    print(printer, " ", NULL);
            }
            print(printer, ") (", NULL);
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                print(printer, "%0:s", FMT_ARGS({ .s = exp->tup.labs[i]->name }));
                if (i != n - 1)
                    print(printer, " ", NULL);
            }
            print(printer, "))", NULL);
            break;
        case EXP_INJ:
            print(printer, "(", NULL);
            print_keyword(printer, "inj");
            print(printer, " ", NULL);
            print_exp(printer, exp->type);
            print(printer, " %0:s", FMT_ARGS({ .s = exp->inj.lab->name }));
            print_exp_or_pat(printer, exp->inj.arg, is_pat);
            print(printer, ")", NULL);
            break;
        case EXP_EXT:
        case EXP_INS:
            print(printer, "(", NULL);
            print_keyword(printer, exp->tag == EXP_EXT ? "ext" : "ins");
            print(printer, " ", NULL);
            print_exp(printer, exp->ext.val);
            print(printer, " %0:s", FMT_ARGS({ .s = exp->ext.lab->name }));
            if (exp->tag == EXP_INS) {
                print(printer, " ", NULL);
                print_exp(printer, exp->ins.elem);
            }
            print(printer, ")", NULL);
            break;
        case EXP_ARROW:
            print(printer, "(", NULL);
            print_keyword(printer, "arrow");
            print(printer, " ", NULL);
            print_var_decl(printer, exp->arrow.var);
            print(printer, " ", NULL);
            print_exp(printer, exp->arrow.codom);
            print(printer, ")", NULL);
            break;
        case EXP_ABS:
            print(printer, "(", NULL);
            print_keyword(printer, "abs");
            print(printer, " ", NULL);
            print_var_decl(printer, exp->abs.var);
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
                print_var_decl(printer, exp->let.vars[i]);
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
                print_exp_or_pat(printer, exp->match.pats[i], true);
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

void print_exp(struct printer* printer, exp_t exp) {
    print_exp_or_pat(printer, exp, false);
}

void dump_exp(exp_t exp) {
    char data[PRINT_BUF_SIZE];
    struct fmtbuf buf = { .data = data, .cap = sizeof(data) };
    struct printer printer = {
        .buf = &buf,
        .tab = "  ",
        .color = is_color_supported(stdout),
        .print_exp = print_exp,
        .indent = 0
    };
    print_exp(&printer, exp);
    dump_fmtbuf(&buf, stdout);
    free_fmtbuf(buf.next);
    printf("\n");
}

void dump_vars(vars_t vars) {
    printf("{");
    if (vars->count > 0) {
        char data[PRINT_BUF_SIZE];
        struct fmtbuf buf = { .data = data, .cap = sizeof(data) };
        struct printer printer = {
            .buf = &buf,
            .tab = "  ",
            .color = is_color_supported(stdout),
            .print_exp = print_exp,
            .indent = 0
        };
        print(&printer, " ", NULL);
        for (size_t i = 0, n = vars->count; i < n; ++i) {
            print_exp(&printer, vars->vars[i]);
            if (i != n - 1)
                print(&printer, ", ", NULL);
        }
        print(&printer, " ", NULL);
        dump_fmtbuf(&buf, stdout);
        free_fmtbuf(buf.next);
    }
    printf("}\n");
}
