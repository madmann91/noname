#include <assert.h>

#include "ir/node.h"
#include "utils/utils.h"

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

static inline void print_lit(struct format_out* out, node_t type, const struct lit* lit) {
    if (type && type->tag != NODE_NAT)
        format(out, "(", NULL);
    format(
        out, lit->tag == LIT_FLOAT ? "%0:hd" : "%1:u",
        FORMAT_ARGS({ .d = lit->float_val }, { .u = lit->int_val }));
    if (type && type->tag != NODE_NAT) {
        format(out, " : ", NULL);
        print_node(out, type);
        format(out, ")", NULL);
    }
}

static inline bool needs_parens(node_t node) {
    switch (node->tag) {
        case NODE_UNI:
        case NODE_STAR:
        case NODE_NAT:
        case NODE_INT:
        case NODE_FLOAT:
        case NODE_VAR:
        case NODE_LIT:
            return false;
        default:
            return true;
    }
}

static void print_exp_or_pat(struct format_out*, node_t, bool);

static void print_pat(struct format_out* out, node_t node) {
    print_exp_or_pat(out, node, true);
}

void print_node(struct format_out* out, node_t node) {
    print_exp_or_pat(out, node, false);
}

static void print_exp_or_pat(struct format_out* out, node_t node, bool is_pat) {
    assert(node);
    switch (node->tag) {
        case NODE_ERR:
            format(out, "%0:$<error", FORMAT_ARGS({ .style = STYLE_ERROR }));
            if (node->type != node && node->type) {
                format(out, " : %0:$", FORMAT_ARGS({ .style = 0 }));
                print_node(out, node->type);
                format(out, "%0:$", FORMAT_ARGS({ .style = STYLE_ERROR }));
            }
            format(out, ">%0:$", FORMAT_ARGS({ .style = 0 }));
            break;
        case NODE_VAR:
            if (is_unbound_var(node))
                format(out, "_", NULL);
            else
                format(out, "%0:s", FORMAT_ARGS({ .s = node->var.label->name }));
            if (is_pat && node->type) {
                format(out, " : ", NULL);
                print_node(out, node->type);
            }
            break;
        case NODE_UNDEF: format(out, "?", NULL);         break;
        case NODE_UNI:   print_keyword(out, "Universe"); break;
        case NODE_STAR:  print_keyword(out, "Type");     break;
        case NODE_NAT:   print_keyword(out, "Nat");      break;
        case NODE_INT:   print_keyword(out, "Int");      break;
        case NODE_FLOAT: print_keyword(out, "Float");    break;
        case NODE_BOT:
        case NODE_TOP:
            assert(node->type);
            print_keyword(out, node->tag == NODE_TOP ? "Top" : "Bot");
            format(out, " ", NULL);
            print_node(out, node->type);
            break;
        case NODE_LIT:
            print_lit(out, node->type, &node->lit);
            break;
        case NODE_SUM:
        case NODE_PROD:
            is_pat = false;
            // fallthrough
        case NODE_RECORD:
            format(out, "{ ", NULL);
            for (size_t i = 0, n = node->record.arg_count; i < n; ++i) {
                format(out, node->tag == NODE_RECORD ? "%0:s = " : "%0:s : ",
                    FORMAT_ARGS({ .s = node->record.labels[i]->name }));
                print_exp_or_pat(out, node->record.args[i], is_pat);
                if (i != n - 1)
                    format(out, ", ", NULL);
            }
            format(out, " }", NULL);
            break;
        case NODE_INJ:
            format(out, "< %0:s = ", FORMAT_ARGS({ .s = node->inj.label->name }));
            print_exp_or_pat(out, node->inj.arg, is_pat);
            format(out, " > ", FORMAT_ARGS({ .s = node->inj.label->name }));
            print_keyword(out, "as");
            format(out, " ", NULL);
            print_node(out, node->type);
            break;
        case NODE_EXT:
            print_node(out, node->ext.val);
            format(out, ".%0:s", FORMAT_ARGS({ .s = node->ext.label->name }));
            break;
        case NODE_INS:
            print_node(out, node->ext.val);
            format(out, ".", NULL);
            print_node(out, node->ins.record);
            break;
        case NODE_ARROW:
            if (is_unbound_var(node->arrow.var)) {
                if (node->arrow.var->type->tag == NODE_ARROW)
                    format(out, "(", NULL);
                print_node(out, node->arrow.var->type);
                if (node->arrow.var->type->tag == NODE_ARROW)
                    format(out, ")", NULL);
                format(out, " -> ", NULL);
            } else {
                print_keyword(out, "forall");
                format(out, " ", NULL);
                print_pat(out, node->arrow.var);
                format(out, " . ", NULL);
            }
            print_node(out, node->arrow.codom);
            break;
        case NODE_FUN:
            print_keyword(out, "fun");
            format(out, " ", NULL);
            print_pat(out, node->fun.var);
            format(out, " => ", NULL);
            print_node(out, node->fun.body);
            break;
        case NODE_APP:
            if (needs_parens(node->app.left))
                format(out, "(", NULL);
            print_node(out, node->app.left);
            if (needs_parens(node->app.left))
                format(out, ")", NULL);
            format(out, " ", NULL);
            if (needs_parens(node->app.right))
                format(out, "(", NULL);
            print_node(out, node->app.right);
            if (needs_parens(node->app.right))
                format(out, ")", NULL);
            break;
        case NODE_LET:
        case NODE_LETREC: {
            bool rec = node->tag == NODE_LETREC;
            print_keyword(out, rec ? "letrec" : "let");
            out->indent++;
            print_newline(out);
            for (size_t i = 0, n = node->let.var_count; i < n; ++i) {
                print_pat(out, node->let.vars[i]);
                format(out, " = ", NULL);
                print_node(out, node->let.vals[i]);
                if (i != n - 1) {
                    format(out, ", ", NULL);
                    print_newline(out);
                }
            }
            print_newline(out);
            print_keyword(out, "in");
            format(out, " ", NULL);
            print_node(out, node->let.body);
            out->indent--;
            break;
        }
        case NODE_MATCH:
            print_keyword(out, "match");
            format(out, " ", NULL);
            print_node(out, node->match.arg);
            format(out, " ", NULL);
            print_keyword(out, "with");
            if (node->match.pat_count > 1) {
                out->indent++;
                print_newline(out);
            } else
                format(out, " ", NULL);
            for (size_t i = 0, n = node->match.pat_count; i < n; ++i) {
                if (n > 1)
                    format(out, "| ", NULL);
                print_pat(out, node->match.pats[i]);
                format(out, " => ", NULL);
                print_node(out, node->match.vals[i]);
                if (i != n - 1)
                    print_newline(out);
            }
            if (node->match.pat_count > 1)
                out->indent--;
            break;
        default:
            assert(false && "invalid expression tag");
            break;
    }
}

void dump_node(node_t node) {
    char data[PRINT_BUF_SIZE];
    struct format_buf buf = { .data = data, .cap = sizeof(data) };
    struct format_out out = {
        .buf = &buf,
        .tab = "  ",
        .color = is_color_supported(stdout),
        .indent = 0
    };
    print_node(&out, node);
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
            .indent = 0
        };
        format(&out, " ", NULL);
        for (size_t i = 0, n = vars->count; i < n; ++i) {
            print_node(&out, vars->vars[i]);
            if (i != n - 1)
                format(&out, ", ", NULL);
        }
        format(&out, " ", NULL);
        dump_format_buf(&buf, stdout);
        free_format_buf(buf.next);
    }
    printf("}\n");
}
