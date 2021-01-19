#include <assert.h>

#include "utils/utils.h"
#include "ir/print.h"

#define PRINT_BUF_SIZE 256

static inline void print_keyword(struct format_out* out, const char* keyword) {
    format(
        out, "%0:$%1:s%2:$",
        FORMAT_ARGS({ .style = STYLE_KEYWORD }, { .s = keyword }, { .style = 0 }));
}

static inline void print_newline(struct format_out* out) {
    format(out, "\n", NULL);
    for (size_t i = 0, n = out->indent; i < n; ++i)
        format(out, "%0:s", FORMAT_ARGS({ .s = out->tab }));
}

static inline void print_lit(struct format_out* out, exp_t type, const struct lit* lit) {
    format(out, "(", NULL);
    print_keyword(out, "lit");
    format(out, " ", NULL);
    print_exp(out, type);
    format(out, " ", NULL);
    format(
        out, lit->tag == LIT_FLOAT ? "%0:hd" : "%1:hu",
        FORMAT_ARGS({ .d = lit->float_val }, { .u = lit->int_val }));
    format(out, ")", NULL);
}

static inline void print_var_decl(struct format_out* out, exp_t var) {
    if (is_unbound_var(var))
        format(out, "(_ : ", NULL);
    else
        format(out, "(%0:s : ", FORMAT_ARGS({ .s = var->var.label->name }));
    print_exp(out, var->type);
    format(out, ")", NULL);
}

static void print_exp_or_pat(struct format_out* out, exp_t exp, bool is_pat) {
    assert(exp->type || exp->tag == EXP_UNI);
    switch (exp->tag) {
        case EXP_ERR:
            format(out, "%0:$<error", FORMAT_ARGS({ .style = STYLE_ERROR }));
            if (exp->type != exp) {
                format(out, " : %0:$", FORMAT_ARGS({ .style = 0 }));
                print_exp(out, exp->type);
                format(out, "%0:$", FORMAT_ARGS({ .style = STYLE_ERROR }));
            }
            format(out, ">%0:$", FORMAT_ARGS({ .style = 0 }));
            break;
        case EXP_VAR:
            if (is_pat)
                print_var_decl(out, exp);
            else
                format(out, "%0:s", FORMAT_ARGS({ .s = exp->var.label->name }));
            break;
        case EXP_UNI:   print_keyword(out, "uni");   break;
        case EXP_STAR:  print_keyword(out, "star");  break;
        case EXP_NAT:   print_keyword(out, "nat");   break;
        case EXP_INT:   print_keyword(out, "int");   break;
        case EXP_FLOAT: print_keyword(out, "float"); break;
        case EXP_BOT:
        case EXP_TOP:
            format(out, "(", NULL);
            print_keyword(out, exp->tag == EXP_TOP ? "top" : "bot");
            format(out, " ", NULL);
            print_exp(out, exp->type);
            format(out, ")", NULL);
            break;
        case EXP_LIT:
            print_lit(out, exp->type, &exp->lit);
            break;
        case EXP_SUM:
        case EXP_PROD:
            is_pat = false;
            // fallthrough
        case EXP_RECORD:
            format(out, "(", NULL);
            print_keyword(
                out,
                exp->tag == EXP_SUM  ? "sum" :
                exp->tag == EXP_PROD ? "prod" : "record");
            format(out, " (", NULL);
            for (size_t i = 0, n = exp->record.arg_count; i < n; ++i) {
                print_exp_or_pat(out, exp->record.args[i], is_pat);
                if (i != n - 1)
                    format(out, " ", NULL);
            }
            format(out, ") (", NULL);
            for (size_t i = 0, n = exp->record.arg_count; i < n; ++i) {
                format(out, "%0:s", FORMAT_ARGS({ .s = exp->record.labels[i]->name }));
                if (i != n - 1)
                    format(out, " ", NULL);
            }
            format(out, "))", NULL);
            break;
        case EXP_INJ:
            format(out, "(", NULL);
            print_keyword(out, "inj");
            format(out, " ", NULL);
            print_exp(out, exp->type);
            format(out, " %0:s", FORMAT_ARGS({ .s = exp->inj.label->name }));
            print_exp_or_pat(out, exp->inj.arg, is_pat);
            format(out, ")", NULL);
            break;
        case EXP_EXT:
        case EXP_INS:
            format(out, "(", NULL);
            print_keyword(out, exp->tag == EXP_EXT ? "ext" : "ins");
            format(out, " ", NULL);
            print_exp(out, exp->ext.val);
            format(out, " %0:s", FORMAT_ARGS({ .s = exp->ext.label->name }));
            if (exp->tag == EXP_INS) {
                format(out, " ", NULL);
                print_exp(out, exp->ins.elem);
            }
            format(out, ")", NULL);
            break;
        case EXP_ARROW:
            format(out, "(", NULL);
            print_keyword(out, "arrow");
            format(out, " ", NULL);
            print_var_decl(out, exp->arrow.var);
            format(out, " ", NULL);
            print_exp(out, exp->arrow.codom);
            format(out, ")", NULL);
            break;
        case EXP_ABS:
            format(out, "(", NULL);
            print_keyword(out, "abs");
            format(out, " ", NULL);
            print_var_decl(out, exp->abs.var);
            format(out, " ", NULL);
            print_exp(out, exp->abs.body);
            format(out, ")", NULL);
            break;
        case EXP_APP:
            format(out, "(", NULL);
            print_exp(out, exp->app.left);
            format(out, " ", NULL);
            print_exp(out, exp->app.right);
            format(out, ")", NULL);
            break;
        case EXP_LET:
        case EXP_LETREC: {
            bool rec = exp->tag == EXP_LETREC;
            format(out, "(", NULL);
            print_keyword(out, rec ? "letrec" : "let");
            out->indent++;
            print_newline(out);
            format(out, "(", NULL);
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i) {
                print_var_decl(out, exp->let.vars[i]);
                if (i != n - 1) {
                    print_newline(out);
                    format(out, " ", NULL);
                }
            }
            format(out, ")", NULL);
            print_newline(out);
            format(out, "(", NULL);
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i) {
                print_exp(out, exp->let.vals[i]);
                if (i != n - 1) {
                    print_newline(out);
                    format(out, " ", NULL);
                }
            }
            format(out, ")", NULL);
            print_newline(out);
            print_exp(out, exp->let.body);
            format(out, ")", NULL);
            out->indent--;
            break;
        }
        case EXP_MATCH:
            format(out, "(", NULL);
            print_keyword(out, "match");
            out->indent++;
            if (exp->match.pat_count > 0)
                print_newline(out);
            else
                format(out, " ", NULL);
            format(out, "(", NULL);
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                format(out, "(", NULL);
                print_keyword(out, "case");
                format(out, " ", NULL);
                print_exp_or_pat(out, exp->match.pats[i], true);
                format(out, " ", NULL);
                print_exp(out, exp->match.vals[i]);
                format(out, ")", NULL);
                if (i != n - 1) {
                    print_newline(out);
                    format(out, " ", NULL);
                }
            }
            format(out, ")", NULL);
            print_newline(out);
            print_exp(out, exp->match.arg);
            format(out, ")", NULL);
            out->indent--;
            break;
        default:
            assert(false && "invalid expression tag");
            break;
    }
}

void print_exp(struct format_out* out, exp_t exp) {
    print_exp_or_pat(out, exp, false);
}

void dump_exp(exp_t exp) {
    char data[PRINT_BUF_SIZE];
    struct format_buf buf = { .data = data, .cap = sizeof(data) };
    struct format_out out = {
        .buf = &buf,
        .tab = "  ",
        .color = is_color_supported(stdout),
        .print_exp = print_exp,
        .indent = 0
    };
    print_exp(&out, exp);
    dump_format_buf(&buf, stdout);
    free_format_buf(buf.next);
    printf("\n");
}

void dump_vars(vars_t vars) {
    printf("{");
    if (vars->count > 0) {
        char data[PRINT_BUF_SIZE];
        struct format_buf buf = { .data = data, .cap = sizeof(data) };
        struct format_out out = {
            .buf = &buf,
            .tab = "  ",
            .color = is_color_supported(stdout),
            .print_exp = print_exp,
            .indent = 0
        };
        format(&out, " ", NULL);
        for (size_t i = 0, n = vars->count; i < n; ++i) {
            print_exp(&out, vars->vars[i]);
            if (i != n - 1)
                format(&out, ", ", NULL);
        }
        format(&out, " ", NULL);
        dump_format_buf(&buf, stdout);
        free_format_buf(buf.next);
    }
    printf("}\n");
}
